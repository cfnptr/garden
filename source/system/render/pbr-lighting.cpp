// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"

#include "math/brdf.hpp"
#include "math/sh.hpp"

using namespace garden;
using namespace math::sh;
using namespace math::ibl;
using namespace math::brdf;

static constexpr uint32 specularSampleCount = 1024;
static constexpr uint32 reflectionsKernelWidth = 21;
static constexpr float reflectionsSigma0 = (reflectionsKernelWidth + 1) / 6.0f;
static constexpr auto reflectionsCoeffCount = GpuProcessSystem::calcGaussCoeffCount(reflectionsKernelWidth);

namespace garden::graphics
{
	struct SpecularItem final
	{
		f32x4 l = f32x4::zero;
		f32x4 nolMip = f32x4::zero;

		SpecularItem(f32x4 l, float nol, float mip) noexcept :
			l(l, 0.0f), nolMip(nol, mip, 0.0f, 0.0f) { }
		SpecularItem() = default;
	};
}

#if 0 // Note: Used to precompute Ki coeffs.
//**********************************************************************************************************************
static uint32 factorial(uint32 x) noexcept
{
	return x == 0 ? 1 : x * factorial(x - 1);
}
static double factorial(int32 n, int32 d) noexcept
{
	d = max(1, d); n = max(1, n);

	auto r = 1.0;
	if (n == d)
	{
		// Note: Intentionally left blank
	}
	else if (n > d)
	{
		while (n > d) { r *= n; n--; }
	}
	else
	{
		while (d > n) { r *= d; d--; }
		r = 1.0 / r;
	}
	return r;
}
static double computeKml(int32 m, int32 l) noexcept
{
	auto k = (2 * l + 1) * factorial(l - m, l + m);
	return sqrt(k) * (M_2_SQRTPI * 0.25);
}
static double computeTruncatedCosSh(uint32 l) noexcept
{
	if (l == 0)
		return M_PI;
	else if (l == 1)
		return 2.0 * M_PI / 3.0;
	else if (l & 1u)
		return 0.0;

	auto l2 = l / 2;
	auto a0 = ((l2 & 1u) ? 1.0 : -1.0) / ((l + 2) * (l - 1));
	auto a1 = factorial(l, l2) / (factorial(l2) * (1u << l));
	return 2.0 * M_PI * a0 * a1;
}

//**********************************************************************************************************************
static void computeKi() noexcept
{
	double ki[shCoeffCount];

	for (uint32 l = 0; l < shBandCount; l++)
	{
		ki[shIndex(0, l)] = computeKml(0, l);

		for (uint32 m = 1; m <= l; m++)
			ki[shIndex(m, l)] = ki[shIndex(-m, l)] = M_SQRT2 * computeKml(m, l);
	}

	for (uint32 l = 0; l < shBandCount; l++)
	{
		auto truncatedCosSh = computeTruncatedCosSh(l);
		ki[shIndex(0, l)] *= truncatedCosSh;

		for (uint32 m = 1; m <= l; m++)
		{
			ki[shIndex(-m, l)] *= truncatedCosSh;
			ki[shIndex( m, l)] *= truncatedCosSh;
		}
	}

	for (uint32 i = 0; i < shCoeffCount; i++)
		cout << i << ". " << setprecision(36) << ki[i] << "\n";
}
#endif

//**********************************************************************************************************************
static constexpr float pow5(float x) noexcept
{
	float x2 = x * x;
	return x2 * x2 * x;
}
static float log4(float x) noexcept
{
	return log2(x) * 0.5f;
}

static float smithGGXCorrelated(float nov, float nol, float a) noexcept
{
	auto a2 = a * a;
	auto lambdaV = nol * sqrt((nov - nov * a2) * nov + a2);
	auto lambdaL = nov * sqrt((nol - nol * a2) * nol + a2);
	return 0.5f / (lambdaV + lambdaL);
}
static float2 dfvMultiscatter(uint32 x, uint32 y) noexcept
{
	constexpr auto invSampleCount = 1.0f / specularSampleCount;
	auto nov = clamp((x + 0.5f) / iblDfgSize, 0.0f, 1.0f);
	auto coord = clamp((iblDfgSize - y + 0.5f) / iblDfgSize, 0.0f, 1.0f);
	auto v = f32x4(sqrt(1.0f - nov * nov), 0.0f, nov);
	auto linearRoughness = coord * coord;

	auto r = float2(0.0f);

	for (uint32 i = 0; i < specularSampleCount; i++)
	{
		auto u = hammersley(i, invSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto voh = dot3(v, h);
		auto l = 2.0f * voh * h - v;
		voh = clamp(voh, 0.0f, 1.0f);
		auto nol = clamp(l.getZ(), 0.0f, 1.0f);
		auto noh = clamp(h.getZ(), 0.0f, 1.0f);

		if (nol > 0.0f)
		{
			auto visibility = smithGGXCorrelated(
				nov, nol, linearRoughness) * nol * (voh / noh);
			auto fc = pow5(1.0f - voh);
			r.x += visibility * fc;
			r.y += visibility;
		}
	}

	return r * (4.0f / specularSampleCount);
}

static float mipToLinearRoughness(uint8 lodCount, uint8 mip) noexcept
{
	constexpr auto a = 2.0f, b = -1.0f;
	auto lod = clamp((float)mip / ((int)lodCount - 1), 0.0f, 1.0f);
	auto perceptualRoughness = clamp((sqrt(
		a * a + 4.0f * b * lod) - a) / (2.0f * b), 0.0f, 1.0f);
	return perceptualRoughness * perceptualRoughness;
}

static uint32 calcSampleCount(uint8 mipLevel) noexcept
{
	return specularSampleCount * (uint32)exp2((float)std::max((int)mipLevel - 1, 0));
}

//**********************************************************************************************************************
static ID<Image> createAccumBuffers(Image::Format format, 
	ID<ImageView>* imageViews, ID<Image>& blurBuffer, const string& debugName)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	Image::Mips mips; mips.assign(PbrLightingSystem::accumBufferCount - 1, { nullptr });
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();

	auto buffer = graphicsSystem->createImage(format, usage, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(buffer, "image.lighting." + debugName + ".buffer");
	blurBuffer = graphicsSystem->createImage(format, usage, { { nullptr } }, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(blurBuffer, "image.lighting." + debugName + ".blurBuffer");

	auto blurBufferView = graphicsSystem->get(blurBuffer);
	imageViews[0] = blurBufferView->getDefaultView();
	SET_RESOURCE_DEBUG_NAME(imageViews[0], "imageView.lighting." + debugName + ".blurBuffer");

	for (uint32 i = 1; i < PbrLightingSystem::accumBufferCount; i++)
	{
		imageViews[i] = graphicsSystem->createImageView(buffer, Image::Type::Texture2D, format, i - 1, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(imageViews[i], "imageView.lighting." + debugName + to_string(i));
	}
	return buffer;
}
static void destroyAccumBuffers(ID<Image> buffer, ID<ImageView>* imageViews, ID<Image> blurBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();

	imageViews[0] = {};
	for (uint32 i = 1; i < PbrLightingSystem::accumBufferCount; i++)
	{
		graphicsSystem->destroy(imageViews[i]);
		imageViews[i] = {};
	}

	graphicsSystem->destroy(blurBuffer);
	graphicsSystem->destroy(buffer);
}

static void createAccumFramebuffers(const ID<ImageView>* imageViews, 
	ID<Framebuffer>* framebuffers, const string& debugName)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();

	vector<Framebuffer::OutputAttachment> colorAttachments
	{ Framebuffer::OutputAttachment(imageViews[0], PbrLightingSystem::accumBufferFlags) };
	framebuffers[0] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[0], "framebuffer.lighting." + debugName + ".blur");

	colorAttachments = { Framebuffer::OutputAttachment(imageViews[1], PbrLightingSystem::accumBufferFlags) };
	framebuffers[1] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[1], "framebuffer.lighting." + debugName + "1");

	framebufferSize = max(framebufferSize / 2u, uint2::one);
	colorAttachments = { Framebuffer::OutputAttachment(imageViews[2], PbrLightingSystem::accumBufferFlags) };
	framebuffers[2] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[2], "framebuffer.lighting." + debugName + "1");
}
static void destroyAccumFramebuffers(ID<Framebuffer>* framebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingSystem::accumBufferCount; i++)
	{
		graphicsSystem->destroy(framebuffers[i]);
		framebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createReflBuffer(vector<ID<ImageView>>& reflImageViews, 
	ID<ImageView>& reflBufferView, vector<ID<DescriptorSet>>& reflBlurDSes, bool useBlur)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto reflBufferSize = graphicsSystem->getScaledFramebufferSize();

	uint8 lodCount = 1, layerCount = 1;
	if (useBlur)
	{
		lodCount = calcMipCount(reflBufferSize); // Note: We don't go lower than 16 texel in one dimension.
		lodCount = (uint8)std::max(std::min(4, (int)lodCount), (int)lodCount - 4);
		reflBlurDSes.resize(lodCount);
		layerCount = 2;
	}
	reflImageViews.resize(lodCount * layerCount);

	Image::Mips mips(lodCount);
	for (uint8 i = 0; i < lodCount; i++)
		mips[i].resize(layerCount);

	auto image = graphicsSystem->createImage(PbrLightingSystem::reflBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::TransferSrc | 
		Image::Usage::Fullscreen, mips, reflBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.reflBuffer");

	for (uint8 mip = 0; mip < lodCount; mip++)
	{
		auto mipOffset = mip * layerCount;
		for (uint8 layer = 0; layer < layerCount; layer++)
		{
			auto imageView = graphicsSystem->createImageView(image, Image::Type::Texture2D, 
				PbrLightingSystem::reflBufferFormat, mip, 1, layer, 1);
			SET_RESOURCE_DEBUG_NAME(imageView, "imageView.lighting.reflBuffer" + to_string(mipOffset + layer));
			reflImageViews[mipOffset + layer] = imageView;
		}
	}

	if (useBlur)
	{
		reflBufferView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, PbrLightingSystem::reflBufferFormat, 0, 0, 0, 1);
		SET_RESOURCE_DEBUG_NAME(reflBufferView, "imageView.lighting.reflBuffer");
	}
	return image;
}
static void createReflFramebuffers(const vector<ID<ImageView>>& reflImageViews, 
	vector<ID<Framebuffer>>& reflFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto mipCount2 = (uint8)reflImageViews.size();
	reflFramebuffers.resize(mipCount2);

	for (uint8 i = 0; i < mipCount2; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(reflImageViews[i], PbrLightingSystem::accumBufferFlags) };
		auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.lighting.reflections" + to_string(i));
		reflFramebuffers[i] = framebuffer;

		if (i % 2 != 0)
			framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}

//**********************************************************************************************************************
static ID<Image> createGiBuffer()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto buffer = graphicsSystem->createImage(PbrLightingSystem::giBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | 
		Image::Usage::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "image.lighting.giBuffer");
	return buffer;
}
static ID<Framebuffer> createGiFramebuffer(ID<Image> giBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto giBufferView = graphicsSystem->get(giBuffer)->getDefaultView();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(giBufferView, PbrLightingSystem::accumBufferFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.lighting.gi");
	return framebuffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getLightingUniforms(ID<Image> dfgLUT, const ID<ImageView>* shadowImageViews, 
	const ID<ImageView>* aoImageViews, ID<ImageView> reflBufferView, ID<Image> giBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto depthBufferView = deferredSystem->getDepthImageView();
	auto dfgLutView = graphicsSystem->get(dfgLUT)->getDefaultView();
	const auto& gColorAttachments = gFramebufferView->getColorAttachments();
	
	graphicsSystem->startRecording(CommandBufferType::Frame);
	graphicsSystem->getEmptyTexture();
	graphicsSystem->getWhiteTexture();
	graphicsSystem->stopRecording();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0] ?
			shadowImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "aoBuffer", DescriptorSet::Uniform(aoImageViews[0] ?
			aoImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "reflBuffer", DescriptorSet::Uniform(reflBufferView ?
			reflBufferView : graphicsSystem->getEmptyTexture()) },
		{ "giBuffer", DescriptorSet::Uniform(giBuffer ?
			graphicsSystem->get(giBuffer)->getDefaultView() : graphicsSystem->getEmptyTexture()) },
		{ "dfgLUT", DescriptorSet::Uniform(dfgLutView) }
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(gColorAttachments[i].imageView ? 
			gColorAttachments[i].imageView : graphicsSystem->getEmptyTexture()));
	}

	return uniforms;
}

static ID<GraphicsPipeline> createLightingPipeline(PbrLightingSystem::Options pbrOptions)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	Pipeline::SpecConstValues specConstValues =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConstValue(pbrOptions.useShadowBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConstValue(pbrOptions.useAoBuffer) },
		{ "USE_REFLECTION_BUFFER", Pipeline::SpecConstValue(pbrOptions.useReflBuffer) },
		{ "USE_REFLECTION_BLUR", Pipeline::SpecConstValue(pbrOptions.useReflBlur) },
		{ "USE_GI_BUFFER", Pipeline::SpecConstValue(pbrOptions.useGiBuffer) },
		{ "USE_CLEAR_COAT_BUFFER", Pipeline::SpecConstValue(deferredSystem->useClearCoat()) },
		{ "USE_EMISSION_BUFFER", Pipeline::SpecConstValue(deferredSystem->useEmission()) }
	};

	ResourceSystem::GraphicsOptions pipelineOptions;
	pipelineOptions.specConstValues = &specConstValues;
	pipelineOptions.useAsyncRecording = deferredSystem->useAsyncRecording();

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"pbr-lighting", deferredSystem->getHdrFramebuffer(), pipelineOptions);
}
static ID<ComputePipeline> createIblSpecularPipeline()
{
	ResourceSystem::ComputeOptions options;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadComputePipeline("ibl-specular", options);
}

//**********************************************************************************************************************
static ID<Image> createDfgLUT()
{
	vector<float2> pixels(iblDfgSize * iblDfgSize);
	auto pixelData = pixels.data();

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([pixelData](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("DFG LUT Create");

			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto y = i / iblDfgSize, x = i - y * iblDfgSize;
				pixelData[i] = dfvMultiscatter(x, (iblDfgSize - 1) - y);
			}
		},
		iblDfgSize * iblDfgSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("DFG LUT Create");

		uint32 index = 0;
		for (int32 y = iblDfgSize - 1; y >= 0; y--)
		{
			for (uint32 x = 0; x < iblDfgSize; x++)
				pixelData[index++] = dfvMultiscatter(x, y);
		}
	}
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16, 
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::ComputeQ, 
		{ { pixelData } }, uint2(iblDfgSize), Image::Strategy::Size, Image::Format::SfloatR32G32);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.dfgLUT");
	return image;
}

static ID<Buffer> createReflKernel()
{
	float2 coeffs[reflectionsCoeffCount];
	GpuProcessSystem::calcGaussCoeffs(reflectionsSigma0, coeffs, reflectionsCoeffCount);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto kernel = graphicsSystem->createBuffer(Buffer::Usage::Storage | Buffer::Usage::TransferDst | 
		Buffer::Usage::TransferQ, Buffer::CpuAccess::None, coeffs, reflectionsCoeffCount * sizeof(float2), 
		Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(kernel, "buffer.uniform.lighting.reflKernel");
	return kernel;
}

//**********************************************************************************************************************
PbrLightingSystem::PbrLightingSystem(Options options, bool setSingleton) : Singleton(setSingleton), options(options)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("PreShadowRender");
	manager->registerEvent("ShadowRender");
	manager->registerEvent("PostShadowRender");
	manager->registerEvent("ShadowRecreate");

	manager->registerEvent("PreAoRender");
	manager->registerEvent("AoRender");
	manager->registerEvent("PostAoRender");
	manager->registerEvent("AoRecreate");

	manager->registerEvent("PreReflRender");
	manager->registerEvent("ReflRender");
	manager->registerEvent("PostReflRender");
	manager->registerEvent("ReflRecreate");

	manager->registerEvent("PreGiRender");
	manager->registerEvent("GiRender");
	manager->registerEvent("PostGiRender");
	manager->registerEvent("GiRecreate");

	ECSM_SUBSCRIBE_TO_EVENT("Init", PbrLightingSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PbrLightingSystem::deinit);
}
PbrLightingSystem::~PbrLightingSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingSystem::deinit);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("PreShadowRender");
		manager->unregisterEvent("ShadowRender");
		manager->unregisterEvent("PostShadowRender");
		manager->unregisterEvent("ShadowRecreate");

		manager->unregisterEvent("PreAoRender");
		manager->unregisterEvent("AoRender");
		manager->unregisterEvent("PostAoRender");
		manager->unregisterEvent("AoRecreate");

		manager->unregisterEvent("PreReflRender");
		manager->unregisterEvent("ReflRender");
		manager->unregisterEvent("PostReflRender");
		manager->unregisterEvent("ReflRecreate");
		
		manager->unregisterEvent("PreGiRender");
		manager->unregisterEvent("GiRender");
		manager->unregisterEvent("PostGiRender");
		manager->unregisterEvent("GiRecreate");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void PbrLightingSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", PbrLightingSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("HdrRender", PbrLightingSystem::hdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", PbrLightingSystem::gBufferRecreate);

	if (!dfgLUT)
		dfgLUT = createDfgLUT();
	if (!shadowBuffer)
		shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
	if (!shadowFramebuffers[0])
		createAccumFramebuffers(shadowImageViews, shadowFramebuffers, "shadow");
	if (!aoBuffer)
		aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
	if (!aoFramebuffers[0])
		createAccumFramebuffers(aoImageViews, aoFramebuffers, "ao");
	if (!reflBuffer)
		reflBuffer = createReflBuffer(reflImageViews, reflBufferView, reflBlurDSes, options.useReflBlur);
	if (reflFramebuffers.empty())
		createReflFramebuffers(reflImageViews, reflFramebuffers);
	if (!giBuffer)
		giBuffer = createGiBuffer();
	if (!giFramebuffer)
		giFramebuffer = createGiFramebuffer(giBuffer);
	if (!reflKernel)
		reflKernel = createReflKernel(); // TODO: share this kernel with generic camera gaussian blur!
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(options);
}
void PbrLightingSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(reflBlurDSes);
		graphicsSystem->destroy(aoBlurDS);
		graphicsSystem->destroy(shadowBlurDS);
		graphicsSystem->destroy(lightingDS);
		graphicsSystem->destroy(reflBlurPipeline);
		graphicsSystem->destroy(aoBlurPipeline);
		graphicsSystem->destroy(shadowBlurPipeline);
		graphicsSystem->destroy(iblSpecularPipeline);
		graphicsSystem->destroy(lightingPipeline);
		graphicsSystem->destroy(reflBufferView);
		graphicsSystem->destroy(reflFramebuffers);
		graphicsSystem->destroy(reflImageViews);
		graphicsSystem->destroy(giFramebuffer);
		graphicsSystem->destroy(giBuffer);
		graphicsSystem->destroy(reflBuffer);
		destroyAccumFramebuffers(aoFramebuffers);
		destroyAccumBuffers(aoBuffer, aoImageViews, aoBlurBuffer);
		destroyAccumFramebuffers(aoFramebuffers);
		destroyAccumBuffers(shadowBuffer, shadowImageViews, shadowBlurBuffer);
		graphicsSystem->destroy(reflKernel);
		graphicsSystem->destroy(dfgLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", PbrLightingSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", PbrLightingSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", PbrLightingSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
static float calcReflLodOffset(uint2 framebufferSize)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto cameraView = CameraSystem::Instance::get()->getComponent(graphicsSystem->camera);
	constexpr float d = 1.0f; // Note: Texel size of the reflection buffer in world units at 1 meter.
	auto texelSizeAtOneMeter = (d * std::tan(cameraView->p.perspective.fieldOfView * 0.5f)) / framebufferSize.y;
	return -std::log2((M_SQRT2 * reflectionsSigma0) * texelSizeAtOneMeter);
}
static void downsampleReflections(const vector<ID<ImageView>>& reflImageViews, 
	const vector<ID<Framebuffer>>& reflFramebuffers, ID<Buffer> reflKernel, 
	ID<GraphicsPipeline>& reflBlurPipeline, vector<ID<DescriptorSet>>& reflBlurDSes)
{
	SET_GPU_DEBUG_LABEL("Reflections Downsample", Color::transparent);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gpuProcessSystem = GpuProcessSystem::Instance::get();
	auto roughnessLodCount = (uint8)(reflImageViews.size() / 2);
	auto image = graphicsSystem->get(reflImageViews[0])->getImage();
	auto reinhard = true;

	Image::BlitRegion blitRegion;
	blitRegion.layerCount = 1;
	
	// TODO: potentially we can reduce VRAM usage by using only one 0 mip level TMP buffer instead of all mips TMP.
	for (uint8 i = 1; i < roughnessLodCount; i++)
	{
		blitRegion.srcMipLevel = i - 1;
		blitRegion.dstMipLevel = i;
		Image::blit(image, image, blitRegion, Sampler::Filter::Linear);

		auto i2 = i * 2;
		gpuProcessSystem->gaussianBlur(reflImageViews[i2], reflFramebuffers[i2], reflFramebuffers[i2 + 1], 
			reflKernel, reflectionsCoeffCount, reinhard, reflBlurPipeline, reflBlurDSes[i]);
		reinhard = false;
	}
}

//**********************************************************************************************************************
void PbrLightingSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("PBR Lighting Pre HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	if (!pipelineView->isReady())
		return;

	if (!lightingDS)
	{
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews, reflBufferView, giBuffer);
		lightingDS = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDS, "descriptorSet.lighting.base");
	}

	auto framebufferView = graphicsSystem->get(reflFramebuffers[0]);
	reflLodOffset = calcReflLodOffset(framebufferView->getSize());

	auto manager = Manager::Instance::get();
	if (options.useShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre Shadows Render");

		auto event = &manager->getEvent("PreShadowRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre AO Render");

		auto event = &manager->getEvent("PreAoRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre Reflections Render");

		auto event = &manager->getEvent("PreReflRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useGiBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre GI Render");

		auto event = &manager->getEvent("PreGiRender");
		if (event->hasSubscribers())
			event->run();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);
	if (options.useShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Shadows Render Pass");

		auto event = &manager->getEvent("ShadowRender");
		if (event->hasSubscribers())
		{
			framebufferView = graphicsSystem->get(shadowFramebuffers[2]);
			{
				SET_GPU_DEBUG_LABEL("Shadows Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::one);
				event->run();
				framebufferView->endRenderPass();
			}
		}

		if (hasAnyShadow)
		{
			GpuProcessSystem::Instance::get()->bilateralBlurD(shadowImageViews[2], shadowFramebuffers[0], 
				shadowFramebuffers[1], blurSharpness, shadowBlurPipeline, shadowBlurDS, 4);
			hasAnyShadow = false;
		}
		else
		{
			auto imageView = graphicsSystem->get(shadowBuffer);
			imageView->clear(float4::one);
		}
	}
	if (options.useAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("AO Render Pass");

		auto event = &manager->getEvent("AoRender");
		if (event->hasSubscribers())
		{
			framebufferView = graphicsSystem->get(aoFramebuffers[2]);
			{
				SET_GPU_DEBUG_LABEL("AO Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::one);
				event->run();
				framebufferView->endRenderPass();
			}
		}

		if (hasAnyAO)
		{
			GpuProcessSystem::Instance::get()->bilateralBlurD(aoImageViews[2], aoFramebuffers[0], 
				aoFramebuffers[1], blurSharpness, aoBlurPipeline, aoBlurDS);
			hasAnyAO = false;
		}
		else
		{
			auto imageView = graphicsSystem->get(aoBuffer);
			imageView->clear(float4::one);
			imageView = graphicsSystem->get(aoBlurBuffer);
			imageView->clear(float4::one);
		}
	}
	if (options.useReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Reflections Render Pass");

		auto event = &manager->getEvent("ReflRender");
		if (event->hasSubscribers())
		{
			framebufferView = graphicsSystem->get(reflFramebuffers[0]);
			{
				SET_GPU_DEBUG_LABEL("Reflection Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::zero);
				event->run();
				framebufferView->endRenderPass();
			}
		}

		if (hasAnyRefl)
		{
			if (options.useReflBlur)
			{
				downsampleReflections(reflImageViews, reflFramebuffers, 
					reflKernel, reflBlurPipeline, reflBlurDSes);
			}
			hasAnyRefl = false;
		}
		else
		{
			auto imageView = graphicsSystem->get(reflBuffer);
			imageView->clear(float4::zero);
		}
	}
	if (options.useGiBuffer)
	{
		SET_CPU_ZONE_SCOPED("GI Render Pass");

		auto event = &manager->getEvent("GiRender");
		if (event->hasSubscribers())
		{
			framebufferView = graphicsSystem->get(giFramebuffer);
			{
				SET_GPU_DEBUG_LABEL("GI Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::zero);
				event->run();
				framebufferView->endRenderPass();
			}
		}

		if (!hasAnyGI)
		{
			auto& cameraConstants = graphicsSystem->getCameraConstants();
			auto skyColor = (float3)cameraConstants.skyColor;
			auto imageView = graphicsSystem->get(giBuffer);
			imageView->clear(float4(skyColor, 1.0f));
		}
	}
	graphicsSystem->stopRecording();

	if (options.useReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post Reflections Render");

		auto event = &manager->getEvent("PostReflRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post Shadows Render");

		auto event = &manager->getEvent("PostShadowRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post AO Render");

		auto event = &manager->getEvent("PostAoRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (options.useGiBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post GI Render");

		auto event = &manager->getEvent("PostGiRender");
		if (event->hasSubscribers())
			event->run();
	}
}

//**********************************************************************************************************************
void PbrLightingSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("PBR Lighting HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	if (!isLoaded)
	{
		auto dfgLutView = graphicsSystem->get(dfgLUT);
		auto reflKernelView = graphicsSystem->get(reflKernel);
		if (!dfgLutView->isReady() || !reflKernelView->isReady() || !lightingDS)
			return;
		isLoaded = true;
	}

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(graphicsSystem->camera);
	if (transformView && !transformView->isActive())
		return;

	auto pbrLightingView = tryGetComponent(graphicsSystem->camera);
	if (!pbrLightingView || !pbrLightingView->cubemap || !pbrLightingView->sh || !pbrLightingView->specular)
		return;

	if (!pbrLightingView->dataReady)
	{
		auto cubemapView = graphicsSystem->get(pbrLightingView->cubemap);
		auto shView = graphicsSystem->get(pbrLightingView->sh);
		auto specularView = graphicsSystem->get(pbrLightingView->specular);
		if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
			return;
		pbrLightingView->dataReady = true;
	}

	if (!pbrLightingView->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet(graphicsSystem->camera, ID<GraphicsPipeline>());
		if (!descriptorSet)
			return;
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.lighting" + to_string(*descriptorSet));
		pbrLightingView->descriptorSet = descriptorSet;
	}

	DescriptorSet::Range descriptorSetRange[2];
	descriptorSetRange[0] = DescriptorSet::Range(lightingDS);
	descriptorSetRange[1] = DescriptorSet::Range(ID<DescriptorSet>(pbrLightingView->descriptorSet));

	const auto& cameraConstants = graphicsSystem->getCameraConstants();

	LightingPC pc;
	pc.uvToWorld = (float4x4)(cameraConstants.invViewProj * f32x4x4::uvToNDC);
	pc.shadowColor = (float4)cameraConstants.shadowColor;
	pc.reflLodOffset = reflLodOffset;
	pc.emissiveCoeff = cameraConstants.emissiveCoeff;
	pc.reflectanceCoeff = reflectanceCoeff;

	SET_GPU_DEBUG_LABEL("PBR Lighting", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSets(descriptorSetRange, 2);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void PbrLightingSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();

	if (shadowBlurDS)
	{
		graphicsSystem->destroy(shadowBlurDS);
		shadowBlurDS = {};
	}
	if (aoBlurDS)
	{
		graphicsSystem->destroy(aoBlurDS);
		aoBlurDS = {};
	}
	if (!reflBlurDSes.empty())
	{
		graphicsSystem->destroy(reflBlurDSes);
		reflBlurDSes = {};
	}

	if (reflBuffer)
	{
		graphicsSystem->destroy(reflBufferView);
		graphicsSystem->destroy(reflImageViews);
		graphicsSystem->destroy(reflBuffer);
		reflBuffer = createReflBuffer(reflImageViews, reflBufferView, reflBlurDSes, options.useReflBlur);
	}
	if (!reflFramebuffers.empty())
	{
		graphicsSystem->destroy(reflFramebuffers);
		createReflFramebuffers(reflImageViews, reflFramebuffers);
	}
	
	if (aoBuffer)
	{
		destroyAccumBuffers(aoBuffer, aoImageViews, aoBlurBuffer);
		aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
	}
	if (aoFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorAttachment = Framebuffer::OutputAttachment(
			aoImageViews[0], PbrLightingSystem::accumBufferFlags);
		auto framebufferView = graphicsSystem->get(aoFramebuffers[0]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		colorAttachment.imageView = aoImageViews[1];
		framebufferView = graphicsSystem->get(aoFramebuffers[1]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferSize = max(framebufferSize / 2u, uint2::one);
		colorAttachment.imageView = aoImageViews[2];
		framebufferView = graphicsSystem->get(aoFramebuffers[2]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}

	if (shadowBuffer)
	{
		destroyAccumBuffers(shadowBuffer, shadowImageViews, shadowBlurBuffer);
		shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
	}
	if (shadowFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorAttachment = Framebuffer::OutputAttachment(
			shadowImageViews[0], PbrLightingSystem::accumBufferFlags);
		auto framebufferView = graphicsSystem->get(shadowFramebuffers[0]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		colorAttachment.imageView = shadowImageViews[1];
		framebufferView = graphicsSystem->get(shadowFramebuffers[1]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		framebufferSize = max(framebufferSize / 2u, uint2::one);
		colorAttachment.imageView = shadowImageViews[2];
		framebufferView = graphicsSystem->get(shadowFramebuffers[2]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}

	if (giBuffer)
	{
		graphicsSystem->destroy(giBuffer);
		giBuffer = createGiBuffer();
	}
	if (giFramebuffer)
	{
		auto giBufferView = graphicsSystem->get(giBuffer)->getDefaultView();
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorAttachment = Framebuffer::OutputAttachment(
			giBufferView, PbrLightingSystem::accumBufferFlags);
		auto framebufferView = graphicsSystem->get(giFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}
	if (lightingDS)
	{
		graphicsSystem->destroy(lightingDS);
		lightingDS = {};
	}

	if (aoBuffer || aoFramebuffers[0])
		Manager::Instance::get()->runEvent("AoRecreate");
	if (shadowBuffer || shadowFramebuffers[0])
		Manager::Instance::get()->runEvent("ShadowRecreate");
	if (reflBuffer || !reflFramebuffers.empty())
		Manager::Instance::get()->runEvent("ReflRecreate");
	if (giBuffer || giFramebuffer)
		Manager::Instance::get()->runEvent("GiRecreate");
}

//**********************************************************************************************************************
void PbrLightingSystem::resetComponent(View<Component> component, bool full)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pbrLightingView = View<PbrLightingComponent>(component);
	graphicsSystem->destroy(pbrLightingView->cubemap);
	graphicsSystem->destroy(pbrLightingView->sh);
	graphicsSystem->destroy(pbrLightingView->specular);
	graphicsSystem->destroy(pbrLightingView->descriptorSet);
	pbrLightingView->cubemap = {};
	pbrLightingView->sh = {};
	pbrLightingView->specular = {};
	pbrLightingView->descriptorSet = {};
}
void PbrLightingSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<PbrLightingComponent>(destination);
	const auto sourceView = View<PbrLightingComponent>(source);
	destinationView->cubemap = sourceView->cubemap;
	destinationView->sh = sourceView->sh;
	destinationView->specular = sourceView->specular;
	destinationView->descriptorSet = sourceView->descriptorSet;
}
string_view PbrLightingSystem::getComponentName() const
{
	return "PBR Lighting";
}

//**********************************************************************************************************************
void PbrLightingSystem::setOptions(Options options)
{
	if (memcmp(&this->options, &options, sizeof(Options)) == 0)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(lightingDS);
	lightingDS = {};

	for (auto& pbrLighting : components)
	{
		if (!pbrLighting.getEntity())
			continue;
		graphicsSystem->destroy(pbrLighting.descriptorSet);
		pbrLighting.descriptorSet = {};
	}

	if (this->options.useShadowBuffer != options.useShadowBuffer)
	{
		if (options.useShadowBuffer)
		{
			shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
			createAccumFramebuffers(shadowImageViews, shadowFramebuffers, "shadow");
		}
		else
		{
			destroyAccumFramebuffers(shadowFramebuffers);
			destroyAccumBuffers(shadowBuffer, shadowImageViews, shadowBlurBuffer);
			shadowBuffer = {};
		}
	}
	if (this->options.useAoBuffer != options.useAoBuffer)
	{
		if (options.useAoBuffer)
		{
			aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
			createAccumFramebuffers(aoImageViews, aoFramebuffers, "ao");
		}
		else
		{
			destroyAccumFramebuffers(aoFramebuffers);
			destroyAccumBuffers(aoBuffer, aoImageViews, aoBlurBuffer);
			aoBuffer = {};
		}
	}
	if (this->options.useReflBuffer != options.useReflBuffer || this->options.useReflBlur != options.useReflBlur)
	{
		if (options.useReflBuffer)
		{
			reflBuffer = createReflBuffer(reflImageViews, reflBufferView, reflBlurDSes, options.useReflBlur);
			createReflFramebuffers(reflImageViews, reflFramebuffers);
		}
		else
		{
			graphicsSystem->destroy(reflBufferView);
			graphicsSystem->destroy(reflFramebuffers);
			graphicsSystem->destroy(reflImageViews);
			graphicsSystem->destroy(reflBuffer);
			reflBuffer = {}; reflImageViews = {}; 
			reflFramebuffers = {}; reflBufferView = {};
		}
	}
	if (this->options.useGiBuffer != options.useGiBuffer)
	{
		if (options.useGiBuffer)
		{
			giBuffer = createGiBuffer();
			giFramebuffer = createGiFramebuffer(giBuffer);
		}
		else
		{
			graphicsSystem->destroy(giFramebuffer);
			graphicsSystem->destroy(giBuffer);
			giBuffer = {}; giFramebuffer = {}; 
		}
	}

	if (lightingPipeline)
	{
		graphicsSystem->destroy(lightingPipeline);
		lightingPipeline = createLightingPipeline(options);
	}

	this->options = options;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> PbrLightingSystem::getLightingPipeline()
{
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(options);
	return lightingPipeline;
}
ID<ComputePipeline> PbrLightingSystem::getIblSpecularPipeline()
{
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();
	return iblSpecularPipeline;
}

const ID<Framebuffer>* PbrLightingSystem::getShadowFramebuffers()
{
	if (!shadowFramebuffers[0] && options.useShadowBuffer)
		createAccumFramebuffers(getShadowImageViews(), shadowFramebuffers, "shadow");
	return shadowFramebuffers;
}
const ID<Framebuffer>* PbrLightingSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0] && options.useAoBuffer)
		createAccumFramebuffers(getAoImageViews(), aoFramebuffers, "ao");
	return aoFramebuffers;
}
const vector<ID<Framebuffer>>& PbrLightingSystem::getReflFramebuffers()
{
	if (reflFramebuffers.empty() && options.useReflBuffer)
		createReflFramebuffers(getReflImageViews(), reflFramebuffers);
	return reflFramebuffers;
}
ID<Framebuffer> PbrLightingSystem::getGiFramebuffer()
{
	if (!giFramebuffer && options.useGiBuffer)
		giFramebuffer = createGiFramebuffer(getGiBuffer());
	return giFramebuffer;
}

//**********************************************************************************************************************
ID<Image> PbrLightingSystem::getDfgLUT()
{
	if (!dfgLUT)
		dfgLUT = createDfgLUT();
	return dfgLUT;
}
ID<Buffer> PbrLightingSystem::getReflKernel()
{
	if (!reflKernel)
		reflKernel = createReflKernel();
	return reflKernel;
}

ID<Image> PbrLightingSystem::getShadowBuffer()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
	return shadowBuffer;
}
ID<Image> PbrLightingSystem::getShadBlurBuffer()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
	return shadowBlurBuffer;
}
ID<Image> PbrLightingSystem::getAoBuffer()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
	return aoBuffer;
}
ID<Image> PbrLightingSystem::getAoBlurBuffer()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
	return aoBlurBuffer;
}
ID<Image> PbrLightingSystem::getReflBuffer()
{
	if (!reflBuffer && options.useReflBuffer)
		reflBuffer = createReflBuffer(reflImageViews, reflBufferView, reflBlurDSes, options.useReflBlur);
	return reflBuffer;
}
ID<Image> PbrLightingSystem::getGiBuffer()
{
	if (!giBuffer && options.useGiBuffer)
		giBuffer = createGiBuffer();
	return giBuffer;
}

const ID<ImageView>* PbrLightingSystem::getShadowImageViews()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createAccumBuffers(shadowBufferFormat, shadowImageViews, shadowBlurBuffer, "shadow");
	return shadowImageViews;
}
const ID<ImageView>* PbrLightingSystem::getAoImageViews()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAccumBuffers(aoBufferFormat, aoImageViews, aoBlurBuffer, "ao");
	return aoImageViews;
}
const vector<ID<ImageView>>& PbrLightingSystem::getReflImageViews()
{
	if (!reflBuffer && options.useReflBuffer)
		reflBuffer = createReflBuffer(reflImageViews, reflBufferView, reflBlurDSes, options.useReflBlur);
	return reflImageViews;
}

//**********************************************************************************************************************
static void calcIblSH(f32x4* shBufferData, const f32x4** faces, uint32 cubemapSize, 
	uint32 taskIndex, uint32 itemOffset, uint32 itemCount)
{
	auto sh = shBufferData + taskIndex * shCoeffCount;
	auto invDim = 1.0f / cubemapSize;
	float shb[shCoeffCount];

	for (uint32 face = 0; face < 6; face++)
	{
		auto pixels = faces[face];
		for (uint32 i = itemOffset; i < itemCount; i++)
		{
			auto y = i / cubemapSize, x = i - y * cubemapSize;
			auto st = coordsToST(uint2(x, y), invDim);
			auto dir = stToDir(st, face);
			auto color = pixels[y * cubemapSize + x];
			color *= calcSolidAngle(st, invDim);
			computeShBasis(dir, shb);

			for (uint32 j = 0; j < shCoeffCount; j++)
				sh[j] += color * shb[j];
		}
	}
}

//**********************************************************************************************************************
static ID<Buffer> generateIblSH(ThreadSystem* threadSystem, const vector<const void*>& _pixels, 
	vector<f32x4>& shBuffer, uint32 cubemapSize, Buffer::Strategy strategy)
{
	auto faces = (const f32x4**)_pixels.data();
	uint32 bufferCount;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		bufferCount = threadPool.getThreadCount();
		shBuffer.resize(bufferCount * shCoeffCount);
		auto shBufferData = shBuffer.data();

		threadPool.addItems([shBufferData, faces, cubemapSize](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("IBL SH Generate");

			calcIblSH(shBufferData, faces, cubemapSize, task.getTaskIndex(),
				task.getItemOffset(), task.getItemCount());
		},
		cubemapSize * cubemapSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("IBL SH Generate");

		bufferCount = 1;
		shBuffer.resize(shCoeffCount);
		calcIblSH(shBuffer.data(), faces, cubemapSize, 0, 0, cubemapSize * cubemapSize);
	}

	auto shBufferData = shBuffer.data();
	if (bufferCount > 1)
	{
		for (uint32 i = 1; i < bufferCount; i++)
		{
			auto data = shBufferData + i * shCoeffCount;
			for (uint32 j = 0; j < shCoeffCount; j++)
				shBufferData[j] += data[j];
		}
		shBuffer.resize(shCoeffCount);
	}

	for (uint32 i = 0; i < shCoeffCount; i++)
		shBufferData[i] *= ki[i];
	
	deringingSH(shBufferData);
	shaderPreprocessSH(shBufferData);

	return GraphicsSystem::Instance::get()->createBuffer(Buffer::Usage::Uniform | 
		Buffer::Usage::TransferDst | Buffer::Usage::ComputeQ, Buffer::CpuAccess::None, 
		shBufferData, shCoeffCount * sizeof(f32x4), Buffer::Location::PreferGPU, strategy);
}

//**********************************************************************************************************************
static void calcIblSpecular(SpecularItem* specularMap, uint32* countBufferData,
	uint32 cubemapSize, uint8 cubemapMipCount, uint8 specularMipCount, uint32 taskIndex)
{
	psize mapOffset = 0;
	auto mipIndex = taskIndex + 1;
	for (uint8 i = 1; i < mipIndex; i++)
		mapOffset += calcSampleCount(i);
	auto map = specularMap + mapOffset;

	auto sampleCount = calcSampleCount(mipIndex);
	auto invSampleCount = 1.0f / sampleCount;
	auto linearRoughness = mipToLinearRoughness(specularMipCount, mipIndex);
	auto logOmegaP = log4((4.0f * (float)M_PI) / (6.0f * cubemapSize * cubemapSize));
	float weight = 0.0f; uint32 count = 0;

	for (uint32 i = 0; i < sampleCount; i++)
	{
		auto u = hammersley(i, invSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto noh = h.getZ(), noh2 = noh * noh;
		auto nol = 2.0f * noh2 - 1.0f;
		auto l = f32x4(2.0f * noh * h.getX(), 2.0f * noh * h.getY(), nol);

		if (nol > 0.0f)
		{
			constexpr auto k = 1.0f; // log4(4.0f);
			auto pdf = ggx(noh, linearRoughness) / 4.0f;
			auto omegaS = 1.0f / (sampleCount * pdf);
			auto level = log4(omegaS) - logOmegaP + k;
			auto mip = clamp(level, 0.0f, (float)(cubemapMipCount - 1));
			map[count++] = SpecularItem(l, nol, mip);
			weight += nol;
		}
	}

	auto invWeight = 1.0f / weight;
	for (uint32 i = 0; i < count; i++)
	{
		auto& nolMip = map[i].nolMip;
		nolMip.setX(nolMip.getX() * invWeight);
	}
		
	qsort(map, count, sizeof(SpecularItem), [](const void* a, const void* b)
	{
		auto aa = (const SpecularItem*)a; auto bb = (const SpecularItem*)b;
		if (aa->nolMip.getX() < bb->nolMip.getX())
			return -1;
		if (aa->nolMip.getX() > bb->nolMip.getX())
			return 1;
		return 0;
	});

	countBufferData[taskIndex] = count;
}

//**********************************************************************************************************************
static ID<Image> generateIblSpecular(ThreadSystem* threadSystem, 
	ID<ComputePipeline> iblSpecularPipeline, ID<Image> cubemap, Memory::Strategy strategy)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto cubemapView = graphicsSystem->get(cubemap);
	auto cubemapSize = cubemapView->getSize().getX();
	auto cubemapFormat = cubemapView->getFormat();
	auto cubemapMipCount = cubemapView->getMipCount();
	auto defaultCubemapView = cubemapView->getDefaultView();
	
	const uint8 maxMipCount = 5; // Note: Optimal value based on filament research.
	auto specularMipCount = std::min(calcMipCount(cubemapSize), maxMipCount);
	Image::Mips mips(specularMipCount);

	for (uint8 i = 0; i < specularMipCount; i++)
		mips[i] = Image::Layers(6);

	auto specular = graphicsSystem->createCubemap(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled |  
		Image::Usage::Storage | Image::Usage::TransferDst, mips, uint2(cubemapSize), strategy);

	uint64 specularCacheSize = 0;
	for (uint8 i = 1; i < specularMipCount; i++)
		specularCacheSize += calcSampleCount(i);
	specularCacheSize *= sizeof(SpecularItem);

	auto cpuSpecularCache = graphicsSystem->createBuffer(
		Buffer::Usage::TransferSrc, Buffer::CpuAccess::RandomReadWrite,
		specularCacheSize, Buffer::Location::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(cpuSpecularCache,
		"buffer.storage.lighting.cpuSpecularCache" + to_string(*cpuSpecularCache));
	auto cpuSpecularCacheView = graphicsSystem->get(cpuSpecularCache);

	vector<uint32> countBuffer(specularMipCount - 1);
	vector<ID<Buffer>> gpuSpecularCache(countBuffer.size());
	auto specularMap = (SpecularItem*)cpuSpecularCacheView->getMap();
	auto countBufferData = countBuffer.data();

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks([=](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("IBL Specular Generate");

			calcIblSpecular(specularMap, countBufferData, cubemapSize,
				cubemapMipCount, specularMipCount, task.getTaskIndex());
		},
		(uint32)countBuffer.size());
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("IBL Specular Generate");

		for (uint32 i = 0; i < (uint32)countBuffer.size(); i++)
		{
			calcIblSpecular(specularMap, countBufferData, cubemapSize,
				cubemapMipCount, specularMipCount, i);
		}
	}

	cpuSpecularCacheView->flush();	
	specularCacheSize = 0;

	SET_GPU_DEBUG_LABEL("IBL Specular Generation", Color::transparent);
	for (uint32 i = 0; i < (uint32)gpuSpecularCache.size(); i++)
	{
		auto cacheSize = countBuffer[i] * sizeof(SpecularItem);
		auto cache = graphicsSystem->createBuffer(Buffer::Usage::Storage | Buffer::Usage::TransferDst, 
			Buffer::CpuAccess::None, cacheSize, Buffer::Location::PreferGPU, Buffer::Strategy::Speed);
		SET_RESOURCE_DEBUG_NAME(cache, "buffer.storage.lighting.gpuSpecularCache" + to_string(*cache));
		gpuSpecularCache[i] = cache;

		Buffer::CopyRegion bufferCopyRegion;
		bufferCopyRegion.size = cacheSize;
		bufferCopyRegion.srcOffset = specularCacheSize;
		Buffer::copy(cpuSpecularCache, cache, bufferCopyRegion);
		specularCacheSize += (uint64)calcSampleCount(i + 1) * sizeof(SpecularItem);
	}

	Image::CopyImageRegion imageCopyRegion;
	imageCopyRegion.layerCount = 6;
	Image::copy(cubemap, specular, imageCopyRegion);

	auto pipelineView = graphicsSystem->get(iblSpecularPipeline);
	pipelineView->bind();

	cubemapSize /= 2;
	for (uint8 i = 0; i < (uint32)gpuSpecularCache.size(); i++)
	{
		auto iblSpecularView = graphicsSystem->createImageView(specular,
			Image::Type::Texture2DArray, cubemapFormat, i + 1, 1, 0, 6);
		DescriptorSet::Uniforms iblSpecularUniforms =
		{
			{ "cubemap", DescriptorSet::Uniform(defaultCubemapView) },
			{ "specular", DescriptorSet::Uniform(iblSpecularView) },
			{ "cache", DescriptorSet::Uniform(gpuSpecularCache[i]) }
		};
		auto iblSpecularDescriptorSet = graphicsSystem->createDescriptorSet(
			iblSpecularPipeline, std::move(iblSpecularUniforms));
		SET_RESOURCE_DEBUG_NAME(iblSpecularDescriptorSet,
			"descriptorSet.lighting.iblSpecular" + to_string(*iblSpecularDescriptorSet));
		pipelineView->bindDescriptorSet(iblSpecularDescriptorSet);

		PbrLightingSystem::SpecularPC pc;
		pc.imageSize = cubemapSize;
		pc.itemCount = countBuffer[i];
		pipelineView->pushConstants(&pc);

		pipelineView->dispatch(uint3(cubemapSize, cubemapSize, 6));

		graphicsSystem->destroy(iblSpecularDescriptorSet);
		graphicsSystem->destroy(iblSpecularView);
		graphicsSystem->destroy(gpuSpecularCache[i]);
		cubemapSize /= 2;
	}

	graphicsSystem->destroy(cpuSpecularCache);
	return specular;
}

//**********************************************************************************************************************
void PbrLightingSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
	Ref<Buffer>& sh, Ref<Image>& specular, Memory::Strategy strategy, vector<f32x4>* shBuffer)
{
	GARDEN_ASSERT(!path.empty());
	
	vector<uint8> left, right, bottom, top, back, front; uint2 size;
	ResourceSystem::Instance::get()->loadCubemapData(path, left, right, bottom, top, back, front, size, true);
	auto cubemapSize = size.x;

	auto mipCount = calcMipCount(cubemapSize);
	Image::Mips mips(mipCount);
	mips[0] = { right.data(), left.data(), top.data(), bottom.data(), front.data(), back.data() };

	for (uint8 i = 1; i < mipCount; i++)
		mips[i] = Image::Layers(6);

	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Graphics);

	cubemap = Ref<Image>(graphicsSystem->createCubemap(Image::Format::SfloatR16G16B16A16, 
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferSrc, 
		mips, size, strategy, Image::Format::SfloatR32G32B32A32));
	SET_RESOURCE_DEBUG_NAME(cubemap, "image.cubemap." + path.generic_string());

	auto cubemapView = graphicsSystem->get(cubemap);
	cubemapView->generateMips();

	vector<f32x4> localSH;
	if (!shBuffer)
		shBuffer = &localSH;

	auto threadSystem = ThreadSystem::Instance::tryGet();
	sh = Ref<Buffer>(generateIblSH(threadSystem, mips[0], *shBuffer, cubemapSize, strategy));
	SET_RESOURCE_DEBUG_NAME(sh, "buffer.sh." + path.generic_string());

	specular = Ref<Image>(generateIblSpecular(threadSystem, 
		iblSpecularPipeline, ID<Image>(cubemap), strategy));
	SET_RESOURCE_DEBUG_NAME(specular, "image.cubemap.specular." + path.generic_string());
	
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
Ref<DescriptorSet> PbrLightingSystem::createDescriptorSet(ID<Entity> entity, 
	ID<Pipeline> pipeline, PipelineType type, uint8 index)
{
	GARDEN_ASSERT(entity);

	auto pbrLightingView = tryGetComponent(entity);
	if (!isLoaded || !pbrLightingView || !pbrLightingView->isReady() || 
		!pbrLightingView->sh || !pbrLightingView->specular)
	{
		return {};
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto specularView = graphicsSystem->get(pbrLightingView->specular)->getDefaultView();

	DescriptorSet::Uniforms iblUniforms =
	{ 
		{ "sh", DescriptorSet::Uniform(ID<Buffer>(pbrLightingView->sh)) },
		{ "specular", DescriptorSet::Uniform(specularView) }
	};

	ID<DescriptorSet> descriptorSet;
	if (!pipeline)
	{
		descriptorSet = graphicsSystem->createDescriptorSet(
			lightingPipeline, std::move(iblUniforms), {}, index);
	}
	else
	{
		if (type == PipelineType::Graphics)
		{
			descriptorSet = graphicsSystem->createDescriptorSet(
				ID<GraphicsPipeline>(pipeline), std::move(iblUniforms), {}, index);
		}
		else if (type == PipelineType::Compute)
		{
			descriptorSet = graphicsSystem->createDescriptorSet(
				ID<ComputePipeline>(pipeline), std::move(iblUniforms), {}, index);
		}
		else if (type == PipelineType::RayTracing)
		{
			descriptorSet = graphicsSystem->createDescriptorSet(
				ID<RayTracingPipeline>(pipeline), std::move(iblUniforms), {}, index);
		}
		else abort();
	}
	return Ref<DescriptorSet>(descriptorSet);
}
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
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"

#include "math/brdf.hpp"
#include "math/sh.hpp"

using namespace garden;
using namespace math::sh;
using namespace math::ibl;
using namespace math::brdf;

namespace garden::graphics
{
	struct SpecularData final
	{
		float4 lMip = float4::zero;
		SpecularData(float3 l, float mip) noexcept : lMip(l, mip) { }
		SpecularData() = default;
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
static float2 dfvMultiscatter(uint32 x, uint32 y, uint32 dfgSize, uint32 sampleCount) noexcept
{
	auto invSampleCount = 1.0f / sampleCount;
	auto nov = clamp((x + 0.5f) / dfgSize, 0.0f, 1.0f);
	auto coord = clamp((dfgSize - y + 0.5f) / dfgSize, 0.0f, 1.0f);
	auto v = f32x4(sqrt(1.0f - nov * nov), 0.0f, nov);
	auto linearRoughness = coord * coord;

	auto r = float2(0.0f);
	for (uint32 i = 0; i < sampleCount; i++)
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
			r.x += visibility * fc; r.y += visibility;
		}
	}
	return r * (4.0f / sampleCount);
}

static float mipToLinearRoughness(uint8 lodCount, uint8 mip) noexcept
{
	constexpr auto a = 2.0f, b = -1.0f;
	auto lod = clamp((float)mip / ((int)lodCount - 1), 0.0f, 1.0f);
	auto perceptualRoughness = clamp((sqrt(
		a * a + 4.0f * b * lod) - a) / (2.0f * b), 0.0f, 1.0f);
	return perceptualRoughness * perceptualRoughness;
}

static uint32 calcIblSampleCount(uint8 mipLevel, uint32 sampleCount) noexcept
{
	return sampleCount * (uint32)exp2((float)std::max((int)mipLevel - 1, 0));
}

//**********************************************************************************************************************
static ID<Image> createAoBuffers(GraphicsSystem* graphicsSystem, ID<ImageView>* aoImageViews, ID<Image>& aoBlurBuffer)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;

	Image::Mips mips; mips.assign(PbrLightingSystem::aoBufferCount - 1, { nullptr });
	auto framebufferSize = graphicsSystem->getScaledFrameSize();

	auto aoBuffer = graphicsSystem->createImage(
		PbrLightingSystem::aoBufferFormat, usage, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(aoBuffer, "image.pbrLighting.aoBuffer");
	aoBlurBuffer = graphicsSystem->createImage(PbrLightingSystem::aoBufferFormat, 
		usage, { { nullptr } }, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(aoBlurBuffer, "image.pbrLighting.aoBlurBuffer");

	auto aoBlurBufferView = graphicsSystem->get(aoBlurBuffer);
	aoImageViews[0] = aoBlurBufferView->getDefaultView();
	SET_RESOURCE_DEBUG_NAME(aoImageViews[0], "imageView.pbrLighting.aoBlurBuffer");

	for (uint8 i = 1; i < PbrLightingSystem::aoBufferCount; i++)
	{
		aoImageViews[i] = graphicsSystem->createImageView(aoBuffer, Image::Type::Texture2D, 
			PbrLightingSystem::aoBufferFormat, i - 1, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(aoImageViews[i], "imageView.pbrLighting.ao" + to_string(i));
	}
	return aoBuffer;
}
static void destroyAoBuffers(GraphicsSystem* graphicsSystem, ID<Image> aoBuffer, 
	ID<ImageView>* aoImageViews, ID<Image> aoBlurBuffer)
{
	aoImageViews[0] = {};
	for (uint8 i = 1; i < PbrLightingSystem::aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoImageViews[i]);
		aoImageViews[i] = {};
	}

	graphicsSystem->destroy(aoBlurBuffer);
	graphicsSystem->destroy(aoBuffer);
}

static void createAoFramebuffers(GraphicsSystem* graphicsSystem, 
	const ID<ImageView>* aoImageViews, ID<Framebuffer>* aoFramebuffers)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	vector<Framebuffer::OutputAttachment> colorAttachments
	{ Framebuffer::OutputAttachment(aoImageViews[0], PbrLightingSystem::framebufferFlags) };
	aoFramebuffers[0] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[0], "framebuffer.pbrLighting.aoBlur");

	colorAttachments = { Framebuffer::OutputAttachment(aoImageViews[1], PbrLightingSystem::framebufferFlags) };
	aoFramebuffers[1] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[1], "framebuffer.pbrLighting.ao1");

	framebufferSize = max(framebufferSize / 2u, uint2::one);
	colorAttachments = { Framebuffer::OutputAttachment(aoImageViews[2], PbrLightingSystem::framebufferFlags) };
	aoFramebuffers[2] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[2], "framebuffer.pbrLighting.ao2");
}
static void destroyAoFramebuffers(GraphicsSystem* graphicsSystem, ID<Framebuffer>* aoFramebuffers)
{
	for (uint8 i = 0; i < PbrLightingSystem::aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoFramebuffers[i]);
		aoFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createShadowBuffers(GraphicsSystem* graphicsSystem, ID<ImageView>* imageViews)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto buffer = graphicsSystem->createImage(PbrLightingSystem::shadowBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen, 
		{ { nullptr, nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "image.pbrLighting.shadowBuffer");

	for (uint8 i = 0; i < PbrLightingSystem::shadowBufferCount; i++)
	{
		imageViews[i] = graphicsSystem->createImageView(buffer, Image::Type::Texture2D, 
			PbrLightingSystem::shadowBufferFormat, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageViews[i], "imageView.pbrLighting.shadow" + to_string(i));
	}
	return buffer;
}
static void destroyShadowBuffers(GraphicsSystem* graphicsSystem, 
	ID<Image> shadowBuffer, ID<ImageView>* shadowImageViews)
{
	for (uint8 i = 0; i < PbrLightingSystem::shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowImageViews[i]);
		shadowImageViews[i] = {};
	}
	graphicsSystem->destroy(shadowBuffer);
}

static void createShadowFramebuffers(GraphicsSystem* graphicsSystem, 
	const ID<ImageView>* shadowImageViews, ID<Framebuffer>* shadowFramebuffers)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	for (uint8 i = 0; i < PbrLightingSystem::shadowBufferCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(shadowImageViews[i], PbrLightingSystem::framebufferFlags) };
		shadowFramebuffers[i] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(shadowFramebuffers[i], "framebuffer.pbrLighting.shadow" + to_string(i));
	}
}
static void destroyShadowFramebuffers(GraphicsSystem* graphicsSystem, ID<Framebuffer>* shadowFramebuffers)
{
	for (uint8 i = 0; i < PbrLightingSystem::shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowFramebuffers[i]);
		shadowFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createReflBuffer(GraphicsSystem* graphicsSystem, ID<ImageView>& reflBufferView, bool useBlur)
{
	auto reflBufferSize = graphicsSystem->getScaledFrameSize();

	uint8 lodCount = 1, layerCount = 1;
	if (useBlur)
	{
		lodCount = calcGgxBlurLodCount(reflBufferSize);
		layerCount = 2;
	}

	Image::Mips mips(lodCount);
	for (uint8 i = 0; i < lodCount; i++)
		mips[i].resize(layerCount);

	auto image = graphicsSystem->createImage(PbrLightingSystem::reflBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::TransferSrc | 
		Image::Usage::Fullscreen, mips, reflBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.pbrLighting.reflBuffer");

	reflBufferView = graphicsSystem->createImageView(image, 
		Image::Type::Texture2D, PbrLightingSystem::reflBufferFormat, 0, 0, 0, 1);
	SET_RESOURCE_DEBUG_NAME(reflBufferView, "imageView.pbrLighting.reflBuffer");
	return image;
}
static void createReflData(GraphicsSystem* graphicsSystem, ID<ImageView> reflBufferView,
	vector<ID<ImageView>>& reflImageViews, vector<ID<Framebuffer>>& reflFramebuffers)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	reflImageViews.resize(1); reflFramebuffers.resize(1);
	reflImageViews[0] = reflBufferView;

	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(reflBufferView, PbrLightingSystem::framebufferFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.pbrLighting.reflections");
	reflFramebuffers[0] = framebuffer;
}

//**********************************************************************************************************************
static ID<Image> createGiBuffer(GraphicsSystem* graphicsSystem)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto buffer = graphicsSystem->createImage(PbrLightingSystem::giBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | 
		Image::Usage::Fullscreen, { { nullptr } }, framebufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(buffer, "image.pbrLighting.giBuffer");
	return buffer;
}
static ID<Framebuffer> createGiFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> giBuffer)
{
	auto giBufferView = graphicsSystem->get(giBuffer)->getDefaultView();
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(giBufferView, PbrLightingSystem::framebufferFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.pbrLighting.gi");
	return framebuffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getLightingUniforms(GraphicsSystem* graphicsSystem, ID<Image> dfgLUT, 
	const ID<ImageView>* shadowImageViews, const ID<ImageView>* aoImageViews, 
	ID<ImageView> reflBufferView, ID<Image> giBuffer)
{
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
		{ "USE_CLEAR_COAT_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useClearCoat) },
		{ "USE_EMISSION_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useEmission) }
	};

	ResourceSystem::GraphicsOptions pipelineOptions;
	pipelineOptions.specConstValues = &specConstValues;
	pipelineOptions.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;

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
static ID<Image> createDfgLUT(GraphicsSystem* graphicsSystem, uint32 dfgSize)
{
	auto sampleCount = (uint32)(dfgSize < 128 ? 2048 : 1024);
	vector<float2> pixels(dfgSize * dfgSize);
	auto pixelData = pixels.data();

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([pixelData, dfgSize, sampleCount](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("DFG LUT Create");

			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto y = i / dfgSize, x = i - y * dfgSize;
				pixelData[i] = dfvMultiscatter(x, (dfgSize - 1) - y, dfgSize, sampleCount);
			}
		},
		dfgSize * dfgSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("DFG LUT Create");

		uint32 index = 0;
		for (int32 y = dfgSize - 1; y >= 0; y--)
		{
			for (uint32 x = 0; x < dfgSize; x++)
				pixelData[index++] = dfvMultiscatter(x, y, dfgSize, sampleCount);
		}
	}
	
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16, 
		Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::ComputeQ, 
		{ { pixelData } }, uint2(dfgSize), Image::Strategy::Size, Image::Format::SfloatR32G32);
	SET_RESOURCE_DEBUG_NAME(image, "image.pbrLighting.dfgLUT");
	return image;
}

static uint32 getDfgLutSize(GraphicsQuality quality) noexcept
{
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: return 64;
		case GraphicsQuality::Ultra: return 256;
		default: return 128;
	}
}
static uint32 getCubemapSize(GraphicsQuality quality) noexcept
{
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: return 32;
		case GraphicsQuality::Low: return 64;
		case GraphicsQuality::Medium: return 128;
		case GraphicsQuality::High: return 256;
		case GraphicsQuality::Ultra: return 512;
		default: abort();
	}
}
static uint32 getSpecularSampleCount(GraphicsQuality quality)
{
	switch (quality)
	{
		case GraphicsQuality::PotatoPC: return 32;
		case GraphicsQuality::Low: return 64;
		case GraphicsQuality::Medium: return 128;
		case GraphicsQuality::High: return 256;
		case GraphicsQuality::Ultra: return 512;
		default: abort();
	}
}

void PbrLightingComponent::setCubemapMode(PbrCubemapMode mode)
{
	if (this->mode == mode)
		return;
	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(descriptorSet);
	graphicsSystem->destroy(specular);
	graphicsSystem->destroy(skybox);
	descriptorSet = {}; specular = {}; skybox = {};
	this->mode = mode;
}

//**********************************************************************************************************************
PbrLightingSystem::PbrLightingSystem(Options options, bool setSingleton) : Singleton(setSingleton), options(options)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("DfgLutRecreate");
	manager->registerEvent("PbrIblRecreate");

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
		manager->unregisterEvent("DfgLutRecreate");
		manager->unregisterEvent("PbrIblRecreate");

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
	ECSM_SUBSCRIBE_TO_EVENT("QualityChange", PbrLightingSystem::qualityChange);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getType("pbrLighting.quality", quality, graphicsQualityNames, (uint32)GraphicsQuality::Count);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!dfgLUT)
		dfgLUT = createDfgLUT(graphicsSystem, getDfgLutSize(quality));
	if (!shadowBuffer)
		shadowBuffer = createShadowBuffers(graphicsSystem, shadowImageViews);
	if (!shadowFramebuffers[0])
		createShadowFramebuffers(graphicsSystem, shadowImageViews, shadowFramebuffers);
	if (!aoBuffer)
		aoBuffer = createAoBuffers(graphicsSystem, aoImageViews, aoBlurBuffer);
	if (!aoFramebuffers[0])
		createAoFramebuffers(graphicsSystem, aoImageViews, aoFramebuffers);
	if (!reflBuffer)
		reflBuffer = createReflBuffer(graphicsSystem, reflBufferView, options.useReflBlur);
	if (!options.useReflBlur && (reflImageViews.empty() || reflFramebuffers.empty()))
		createReflData(graphicsSystem, reflBufferView, reflImageViews, reflFramebuffers);
	if (!giBuffer)
		giBuffer = createGiBuffer(graphicsSystem);
	if (!giFramebuffer)
		giFramebuffer = createGiFramebuffer(graphicsSystem, giBuffer);
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
		destroyAoFramebuffers(graphicsSystem, aoFramebuffers);
		destroyAoBuffers(graphicsSystem, aoBuffer, aoImageViews, aoBlurBuffer);
		destroyShadowFramebuffers(graphicsSystem, shadowFramebuffers);
		destroyShadowBuffers(graphicsSystem, shadowBuffer, shadowImageViews);
		graphicsSystem->destroy(dfgLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", PbrLightingSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", PbrLightingSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", PbrLightingSystem::gBufferRecreate);
		ECSM_UNSUBSCRIBE_FROM_EVENT("QualityChange", PbrLightingSystem::qualityChange);
	}
}

//**********************************************************************************************************************
void PbrLightingSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("PBR Lighting Pre HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(lightingPipeline);
	auto dfgLutView = graphicsSystem->get(dfgLUT);
	if (!pipelineView->isReady() || !dfgLutView->isReady())
		return;

	if (!lightingDS)
	{
		auto uniforms = getLightingUniforms(graphicsSystem, dfgLUT, 
			shadowImageViews, aoImageViews, reflBufferView, giBuffer);
		lightingDS = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDS, "descriptorSet.pbrLighting.base");
	}
	
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
		SET_GPU_DEBUG_LABEL("Shadows Pass");

		auto event = &manager->getEvent("ShadowRender");
		if (event->hasSubscribers())
		{
			RenderPass renderPass(shadowFramebuffers[0], float4::one);
			event->run();
		}

		if (hasAnyShadow)
		{
			if (quality > GraphicsQuality::Low)
			{
				GpuProcessSystem::Instance::get()->bilateralBlurD(shadowImageViews[0], shadowFramebuffers[0], 
						shadowFramebuffers[1], blurSharpness, shadowBlurPipeline, shadowBlurDS, 4);
			}
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
		SET_GPU_DEBUG_LABEL("AO Pass");

		auto event = &manager->getEvent("AoRender");
		if (event->hasSubscribers())
		{
			auto framebufferView = graphicsSystem->get(aoFramebuffers[2]);
			RenderPass renderPass(aoFramebuffers[2], float4::one);
			event->run();
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
		SET_GPU_DEBUG_LABEL("Reflection Pass");

		auto event = &manager->getEvent("ReflRender");
		if (event->hasSubscribers())
		{
			RenderPass renderPass(reflFramebuffers[0], float4::zero);
			event->run();
		}

		if (hasAnyRefl)
		{
			if (options.useReflBlur)
			{
				auto gpuProcessSystem = GpuProcessSystem::Instance::get();
				gpuProcessSystem->prepareGgxBlur(reflBuffer, reflImageViews, reflFramebuffers);
				gpuProcessSystem->ggxBlur(reflBuffer, reflImageViews, 
					reflFramebuffers, reflBlurPipeline, reflBlurDSes);
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
		SET_GPU_DEBUG_LABEL("GI Pass");

		auto event = &manager->getEvent("GiRender");
		if (event->hasSubscribers())
		{
			RenderPass renderPass(giFramebuffer, float4::zero);
			event->run();
		}

		if (!hasAnyGI)
		{
			auto& cc = graphicsSystem->getCommonConstants();
			auto skyColor = (float3)cc.skyColor;
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

	if (!lightingDS)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pbrLightingView = tryGetComponent(graphicsSystem->camera);
	if (!pbrLightingView)
		return;

	if (pbrLightingView->mode == PbrCubemapMode::Static)
	{
		if (!pbrLightingView->skybox || !pbrLightingView->sh || !pbrLightingView->specular)
			return;

		auto cubemapView = graphicsSystem->get(pbrLightingView->skybox);
		auto shView = graphicsSystem->get(pbrLightingView->sh);
		auto specularView = graphicsSystem->get(pbrLightingView->specular);
		if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
			return;
	}

	if (!pbrLightingView->skybox)
	{
		auto cubemapSize = getCubemapSize(quality);
		auto mipCount = calcMipCount(cubemapSize);
		Image::Mips mips(mipCount);
		for (uint8 i = 0; i < mipCount; i++)
			mips[i] = Image::Layers(Image::cubemapFaceCount);

		pbrLightingView->skybox = Ref<Image>(graphicsSystem->createCubemap(
			Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled | Image::Usage::ColorAttachment | 
			Image::Usage::TransferSrc | Image::Usage::TransferDst, mips, uint2(cubemapSize), Image::Strategy::Size));
		SET_RESOURCE_DEBUG_NAME(pbrLightingView->skybox, 
			"image.pbrLighting.skybox" + to_string(*pbrLightingView->skybox));
	}
	if (!pbrLightingView->sh)
	{
		pbrLightingView->sh = Ref<Buffer>(graphicsSystem->createBuffer(
			Buffer::Usage::Uniform | Buffer::Usage::TransferDst, Buffer::CpuAccess::None, 
			shBinarySize, Buffer::Location::PreferGPU, Buffer::Strategy::Size));
		SET_RESOURCE_DEBUG_NAME(pbrLightingView->sh, "buffer.uniform.sh" + to_string(*pbrLightingView->sh));
	}
	if (!pbrLightingView->specular)
	{
		auto cubemapSize = getCubemapSize(quality);
		auto specularMipCount = calcSpecularMipCount(cubemapSize);
		Image::Mips mips(specularMipCount);
		for (uint8 i = 0; i < specularMipCount; i++)
			mips[i] = Image::Layers(Image::cubemapFaceCount);

		pbrLightingView->specular = Ref<Image>(graphicsSystem->createCubemap(
			Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled |  Image::Usage::Storage | 
			Image::Usage::TransferDst, mips, uint2(cubemapSize), Buffer::Strategy::Size));
		SET_RESOURCE_DEBUG_NAME(pbrLightingView->specular, 
			"image.pbrLighting.specular" + to_string(*pbrLightingView->specular));
	}

	if (!pbrLightingView->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet(graphicsSystem->camera, ID<GraphicsPipeline>());
		if (!descriptorSet)
			return;
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.pbrLighting" + to_string(*descriptorSet));
		pbrLightingView->descriptorSet = descriptorSet;
	}

	DescriptorSet::Range descriptorSetRange[2];
	descriptorSetRange[0] = DescriptorSet::Range(lightingDS);
	descriptorSetRange[1] = DescriptorSet::Range(ID<DescriptorSet>(pbrLightingView->descriptorSet));

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	const auto& cc = graphicsSystem->getCommonConstants();

	LightingPC pc;
	pc.uvToWorld = (float4x4)(cc.invViewProj * f32x4x4::uvToNDC);
	pc.shadowColor = (float4)cc.shadowColor;
	pc.ggxLodOffset = cc.ggxLodOffset;
	pc.emissiveCoeff = cc.emissiveCoeff;
	pc.reflectanceCoeff = reflectanceCoeff;

	SET_GPU_DEBUG_LABEL("PBR Lighting");
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
		graphicsSystem->destroy(reflBuffer);
		graphicsSystem->destroy(reflBufferView);
		reflBuffer = createReflBuffer(graphicsSystem, reflBufferView, options.useReflBlur);
		if (options.useReflBlur)
		{
			graphicsSystem->destroy(reflImageViews);
			reflImageViews = {};
		}
		else reflImageViews[0] = reflBufferView;
	}
	if (!reflFramebuffers.empty())
	{
		if (options.useReflBlur)
		{
			graphicsSystem->destroy(reflFramebuffers);
			reflFramebuffers = {};
		}
		else
		{
			auto framebufferSize = graphicsSystem->getScaledFrameSize();
			auto colorAttachment = Framebuffer::OutputAttachment(
				reflImageViews[0], PbrLightingSystem::framebufferFlags);
			auto framebufferView = graphicsSystem->get(reflFramebuffers[0]);
			framebufferView->update(framebufferSize, &colorAttachment, 1);
		}
	}
	
	if (aoBuffer)
	{
		destroyAoBuffers(graphicsSystem, aoBuffer, aoImageViews, aoBlurBuffer);
		aoBuffer = createAoBuffers(graphicsSystem, aoImageViews, aoBlurBuffer);
	}
	if (aoFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		auto colorAttachment = Framebuffer::OutputAttachment(
			aoImageViews[0], PbrLightingSystem::framebufferFlags);
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
		destroyShadowBuffers(graphicsSystem, shadowBuffer, shadowImageViews);
		shadowBuffer = createShadowBuffers(graphicsSystem, shadowImageViews);
	}
	if (shadowFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		for (uint8 i = 0; i < PbrLightingSystem::shadowBufferCount; i++)
		{
			auto colorAttachment = Framebuffer::OutputAttachment(
				shadowImageViews[i], PbrLightingSystem::framebufferFlags);
			auto framebufferView = graphicsSystem->get(shadowFramebuffers[i]);
			framebufferView->update(framebufferSize, &colorAttachment, 1);
		}
	}

	if (giBuffer)
	{
		graphicsSystem->destroy(giBuffer);
		giBuffer = createGiBuffer(graphicsSystem);
	}
	if (giFramebuffer)
	{
		auto giBufferView = graphicsSystem->get(giBuffer)->getDefaultView();
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		auto colorAttachment = Framebuffer::OutputAttachment(
			giBufferView, PbrLightingSystem::framebufferFlags);
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

void PbrLightingSystem::qualityChange()
{
	setQuality(GraphicsSystem::Instance::get()->quality);
}

//**********************************************************************************************************************
void PbrLightingSystem::resetComponent(View<Component> component, bool full)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pbrLightingView = View<PbrLightingComponent>(component);
	graphicsSystem->destroy(pbrLightingView->skybox);
	graphicsSystem->destroy(pbrLightingView->sh);
	graphicsSystem->destroy(pbrLightingView->specular);
	graphicsSystem->destroy(pbrLightingView->descriptorSet);
	pbrLightingView->skybox = {};
	pbrLightingView->sh = {};
	pbrLightingView->specular = {};
	pbrLightingView->descriptorSet = {};
}
void PbrLightingSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<PbrLightingComponent>(destination);
	const auto sourceView = View<PbrLightingComponent>(source);
	destinationView->skybox = sourceView->skybox;
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
	graphicsSystem->destroy(lightingDS); lightingDS = {};

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
			shadowBuffer = createShadowBuffers(graphicsSystem, shadowImageViews);
			createShadowFramebuffers(graphicsSystem, shadowImageViews, shadowFramebuffers);
		}
		else
		{
			graphicsSystem->destroy(shadowBlurDS);
			destroyShadowFramebuffers(graphicsSystem, shadowFramebuffers);
			destroyShadowBuffers(graphicsSystem, shadowBuffer, shadowImageViews);
			shadowBlurDS = {}; shadowBuffer = {};
		}
	}
	if (this->options.useAoBuffer != options.useAoBuffer)
	{
		if (options.useAoBuffer)
		{
			aoBuffer = createAoBuffers(graphicsSystem, aoImageViews, aoBlurBuffer);
			createAoFramebuffers(graphicsSystem, aoImageViews, aoFramebuffers);
		}
		else
		{
			graphicsSystem->destroy(aoBlurDS);
			destroyAoFramebuffers(graphicsSystem, aoFramebuffers);
			destroyAoBuffers(graphicsSystem, aoBuffer, aoImageViews, aoBlurBuffer);
			aoBlurDS = {}; aoBuffer = {};
		}
	}
	if (this->options.useReflBuffer != options.useReflBuffer || this->options.useReflBlur != options.useReflBlur)
	{
		if (options.useReflBuffer)
		{
			reflBuffer = createReflBuffer(graphicsSystem, reflBufferView, options.useReflBlur);
			if (!options.useReflBlur)
				createReflData(graphicsSystem, reflBufferView, reflImageViews, reflFramebuffers);
		}
		else
		{
			graphicsSystem->destroy(reflBlurDSes);
			graphicsSystem->destroy(reflFramebuffers);
			graphicsSystem->destroy(reflBufferView);
			graphicsSystem->destroy(reflImageViews);
			graphicsSystem->destroy(reflBuffer);
			reflBlurDSes = {}; reflFramebuffers = {}; 
			reflBufferView = {}; reflImageViews = {}; reflBuffer = {}; 
		}
	}
	if (this->options.useGiBuffer != options.useGiBuffer)
	{
		if (options.useGiBuffer)
		{
			giBuffer = createGiBuffer(graphicsSystem);
			giFramebuffer = createGiFramebuffer(graphicsSystem, giBuffer);
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

void PbrLightingSystem::setQuality(GraphicsQuality quality)
{
	if (this->quality == quality)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (dfgLUT)
	{
		auto dfgLutView = graphicsSystem->get(dfgLUT);
		if (dfgLutView->getSize() != getDfgLutSize(quality))
		{
			graphicsSystem->destroy(lightingDS); lightingDS = {};
			graphicsSystem->destroy(dfgLUT);
			dfgLUT = createDfgLUT(graphicsSystem, getDfgLutSize(quality));
		}
	}

	for (auto& component : components)
	{
		if (component.getCubemapMode() != PbrCubemapMode::Dynamic)
			continue;

		graphicsSystem->destroy(component.descriptorSet);
		graphicsSystem->destroy(component.specular);
		graphicsSystem->destroy(component.skybox);
		component.descriptorSet = {}; component.specular = {}; component.skybox = {};
	}

	this->quality = quality;

	auto manager = Manager::Instance::get();
	manager->runEvent("PbrIblRecreate");
	manager->runEvent("DfgLutRecreate");
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
		createShadowFramebuffers(GraphicsSystem::Instance::get(), getShadowImageViews(), shadowFramebuffers);
	return shadowFramebuffers;
}
const ID<Framebuffer>* PbrLightingSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0] && options.useAoBuffer)
		createAoFramebuffers(GraphicsSystem::Instance::get(), getAoImageViews(), aoFramebuffers);
	return aoFramebuffers;
}
const vector<ID<Framebuffer>>& PbrLightingSystem::getReflFramebuffers()
{
	if (reflFramebuffers.empty() && options.useReflBuffer)
	{
		if (options.useReflBlur)
			GpuProcessSystem::Instance::get()->prepareGgxBlur(getReflBuffer(), reflImageViews, reflFramebuffers);
		else createReflData(GraphicsSystem::Instance::get(), getReflBufferView(), reflImageViews, reflFramebuffers);
	}
	return reflFramebuffers;
}
ID<Framebuffer> PbrLightingSystem::getGiFramebuffer()
{
	if (!giFramebuffer && options.useGiBuffer)
		giFramebuffer = createGiFramebuffer(GraphicsSystem::Instance::get(), getGiBuffer());
	return giFramebuffer;
}

//**********************************************************************************************************************
ID<Image> PbrLightingSystem::getDfgLUT()
{
	if (!dfgLUT)
		dfgLUT = createDfgLUT(GraphicsSystem::Instance::get(), getDfgLutSize(quality));
	return dfgLUT;
}

ID<Image> PbrLightingSystem::getShadowBuffer()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createShadowBuffers(GraphicsSystem::Instance::get(), shadowImageViews);
	return shadowBuffer;
}
ID<Image> PbrLightingSystem::getShadBlurBuffer()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createShadowBuffers(GraphicsSystem::Instance::get(), shadowImageViews);
	return shadowBlurBuffer;
}
ID<Image> PbrLightingSystem::getAoBuffer()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAoBuffers(GraphicsSystem::Instance::get(), aoImageViews, aoBlurBuffer);
	return aoBuffer;
}
ID<Image> PbrLightingSystem::getAoBlurBuffer()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAoBuffers(GraphicsSystem::Instance::get(), aoImageViews, aoBlurBuffer);
	return aoBlurBuffer;
}
ID<Image> PbrLightingSystem::getReflBuffer()
{
	if (!reflBuffer && options.useReflBuffer)
		reflBuffer = createReflBuffer(GraphicsSystem::Instance::get(), reflBufferView, options.useReflBlur);
	return reflBuffer;
}
ID<Image> PbrLightingSystem::getGiBuffer()
{
	if (!giBuffer && options.useGiBuffer)
		giBuffer = createGiBuffer(GraphicsSystem::Instance::get());
	return giBuffer;
}

const ID<ImageView>* PbrLightingSystem::getShadowImageViews()
{
	if (!shadowBuffer && options.useShadowBuffer)
		shadowBuffer = createShadowBuffers(GraphicsSystem::Instance::get(), shadowImageViews);
	return shadowImageViews;
}
const ID<ImageView>* PbrLightingSystem::getAoImageViews()
{
	if (!aoBuffer && options.useAoBuffer)
		aoBuffer = createAoBuffers(GraphicsSystem::Instance::get(), aoImageViews, aoBlurBuffer);
	return aoImageViews;
}
const vector<ID<ImageView>>& PbrLightingSystem::getReflImageViews()
{
	if (reflImageViews.empty() && options.useReflBuffer)
	{
		if (options.useReflBlur)
			GpuProcessSystem::Instance::get()->prepareGgxBlur(getReflBuffer(), reflImageViews, reflFramebuffers);
		else createReflData(GraphicsSystem::Instance::get(), getReflBufferView(), reflImageViews, reflFramebuffers);
	}
	return reflImageViews;
}
ID<ImageView> PbrLightingSystem::getReflBufferView()
{
	if (!reflBuffer && options.useReflBuffer)
		reflBuffer = createReflBuffer(GraphicsSystem::Instance::get(), reflBufferView, options.useReflBlur);
	return reflBufferView;
}

//**********************************************************************************************************************
static void calcIblSH(f32x4* shBufferData, const float4* const* faces, 
	uint32 cubemapSize, uint32 taskIndex, uint32 itemOffset, uint32 itemCount)
{
	auto sh = shBufferData + taskIndex * shCoeffCount;
	auto invDim = 1.0f / cubemapSize;
	float shb[shCoeffCount];

	for (uint32 face = 0; face < Image::cubemapFaceCount; face++)
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
				sh[j] += f32x4(color) * shb[j];
		}
	}
}
static void calcIblSpecular(SpecularData* specularMap, float* weightBufferData, uint32* countBufferData, 
	uint32 cubemapSize, uint32 sampleCount, uint8 skyboxMipCount, uint8 specularMipCount, uint32 taskIndex)
{
	psize mapOffset = 0;
	auto mipIndex = taskIndex + 1;
	for (uint8 i = 1; i < mipIndex; i++)
		mapOffset += calcIblSampleCount(i, sampleCount);
	auto map = specularMap + mapOffset;

	auto iblSampleCount = calcIblSampleCount(mipIndex, sampleCount);
	auto invIblSampleCount = 1.0f / iblSampleCount;
	auto linearRoughness = mipToLinearRoughness(specularMipCount, mipIndex);
	auto logOmegaP = log4((4.0f * (float)M_PI) / (6.0f * cubemapSize * cubemapSize));
	float weight = 0.0f; uint32 count = 0;

	for (uint32 i = 0; i < iblSampleCount; i++)
	{
		auto u = hammersley(i, invIblSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto noh = h.getZ(), noh2 = noh * noh, nol = 2.0f * noh2 - 1.0f;

		if (nol > 0.0f)
		{
			constexpr auto k = 1.0f; // log4(4.0f);
			auto pdf = ggx(noh, linearRoughness) / 4.0f;
			auto omegaS = 1.0f / (iblSampleCount * pdf);
			auto level = log4(omegaS) - logOmegaP + k;
			auto mip = clamp(level, 0.0f, (float)(skyboxMipCount - 1));
			auto l = float3(2.0f * noh * h.getX(), 2.0f * noh * h.getY(), nol);
			map[count++] = SpecularData(l, mip); weight += nol;
		}
	}
		
	qsort(map, count, sizeof(SpecularData), [](const void* a, const void* b)
	{
		auto aa = (const SpecularData*)a; auto bb = (const SpecularData*)b;
		if (aa->lMip.z < bb->lMip.z)
			return -1;
		if (aa->lMip.z > bb->lMip.z)
			return 1;
		return 0;
	});

	weightBufferData[taskIndex] = 1.0f / weight;
	countBufferData[taskIndex] = count;
}

//**********************************************************************************************************************
ID<Buffer> PbrLightingSystem::createSpecularCache(uint32 cubemapSize, 
	vector<float>& iblWeightBuffer, vector<uint32>& iblCountBuffer)
{
	GARDEN_ASSERT(cubemapSize > 0);
	auto skyboxMipCount = calcMipCount(cubemapSize);
	auto specularMipCount = calcSpecularMipCount(cubemapSize);
	auto sampleCount = getSpecularSampleCount(quality);

	uint64 specularCacheSize = 0;
	for (uint8 i = 1; i < specularMipCount; i++)
		specularCacheSize += calcIblSampleCount(i, sampleCount);
	specularCacheSize *= sizeof(SpecularData);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto stagingBuffer = graphicsSystem->createBuffer(Buffer::Usage::TransferSrc, 
		Buffer::CpuAccess::RandomReadWrite, specularCacheSize, Buffer::Location::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.specularCache" + to_string(*stagingBuffer));

	auto stagingView = graphicsSystem->get(stagingBuffer);
	auto specularMap = (SpecularData*)stagingView->getMap();
	iblWeightBuffer.resize(specularMipCount - 1);
	iblCountBuffer.resize(specularMipCount - 1);
	auto weightBufferData = iblWeightBuffer.data();
	auto countBufferData = iblCountBuffer.data();

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks([=](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("IBL Specular Generate");
			calcIblSpecular(specularMap, weightBufferData, countBufferData, cubemapSize, 
				sampleCount, skyboxMipCount, specularMipCount, task.getTaskIndex());
		},
		(uint32)iblCountBuffer.size());
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("IBL Specular Generate");
		for (uint32 i = 0; i < (uint32)iblCountBuffer.size(); i++)
		{
			calcIblSpecular(specularMap, weightBufferData, countBufferData, 
				cubemapSize, sampleCount, skyboxMipCount, specularMipCount, i);
		}
	}

	stagingView->flush();

	uint64 gpuCacheSize = 0;
	for (uint8 i = 0; i < (uint8)iblCountBuffer.size(); i++)
		gpuCacheSize += iblCountBuffer[i] * sizeof(SpecularData);

	auto specularCache = graphicsSystem->createBuffer(Buffer::Usage::Storage | Buffer::Usage::TransferDst, 
		Buffer::CpuAccess::None, gpuCacheSize, Buffer::Location::PreferGPU, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(specularCache, "buffer.storage.specularCache" + to_string(*specularCache));

	auto stopRecording = false;
	if (!graphicsSystem->isRecording())
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		stopRecording = true;
	}

	specularCacheSize = gpuCacheSize = 0;
	Buffer::CopyRegion bufferCopyRegion;

	BEGIN_GPU_DEBUG_LABEL("Generate Specular Cache");
	for (uint8 i = 0; i < (uint8)iblCountBuffer.size(); i++)
	{
		bufferCopyRegion.size = iblCountBuffer[i] * sizeof(SpecularData);
		bufferCopyRegion.srcOffset = specularCacheSize;
		bufferCopyRegion.dstOffset = gpuCacheSize;
		Buffer::copy(stagingBuffer, specularCache, bufferCopyRegion);

		specularCacheSize += (uint64)calcIblSampleCount(
			i + 1, sampleCount) * sizeof(SpecularData);
		gpuCacheSize += bufferCopyRegion.size;
	}
	END_GPU_DEBUG_LABEL();

	if (stopRecording)
		graphicsSystem->stopRecording();
	graphicsSystem->destroy(stagingBuffer);
	return specularCache;
}

//**********************************************************************************************************************
void PbrLightingSystem::createIblSpecularViews(ID<Image> specular, vector<ID<ImageView>>& specularViews)
{
	GARDEN_ASSERT(specular);
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto specularView = graphicsSystem->get(specular);
	auto specularFormat = specularView->getFormat();
	auto count = (uint8)(specularView->getMipCount() - 1);
	specularViews.resize(count);

	for (uint8 i = 0; i < count; i++)
	{
		auto specularView = graphicsSystem->createImageView(specular,
			Image::Type::Texture2DArray, specularFormat, i + 1, 1, 0, Image::cubemapFaceCount);
		SET_RESOURCE_DEBUG_NAME(specularView, "imageView.pbrLighting.specular" + 
			to_string(*specularView) + "_" + to_string(i));
		specularViews[i] = specularView;
	}
}
void PbrLightingSystem::createIblDescriptorSets(ID<Image> skybox, ID<Buffer> specularCache, 
	const vector<ID<ImageView>>& specularViews, vector<ID<DescriptorSet>>& iblDescriptorSets)
{
	GARDEN_ASSERT(skybox);
	GARDEN_ASSERT(specularCache);
	GARDEN_ASSERT(!specularViews.empty());

	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto skyboxView = graphicsSystem->get(skybox)->getDefaultView();
	iblDescriptorSets.resize(specularViews.size());

	for (uint8 i = 0; i < (uint8)specularViews.size(); i++)
	{
		DescriptorSet::Uniforms iblSpecularUniforms =
		{
			{ "skybox", DescriptorSet::Uniform(skyboxView) },
			{ "specular", DescriptorSet::Uniform(specularViews[i]) },
			{ "cache", DescriptorSet::Uniform(specularCache) }
		};

		auto descriptorSet = graphicsSystem->createDescriptorSet(iblSpecularPipeline, std::move(iblSpecularUniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.pbrLighting.iblSpecular" + to_string(*descriptorSet));
		iblDescriptorSets[i] = descriptorSet;
	}
}
void PbrLightingSystem::dispatchIblSpecular(ID<Image> skybox, ID<Image> specular,
	const vector<float>& iblWeightBuffer, const vector<uint32>& iblCountBuffer, 
	const vector<ID<DescriptorSet>>& iblDescriptorSets, int8 face)
{
	GARDEN_ASSERT(skybox);
	GARDEN_ASSERT(specular);
	GARDEN_ASSERT(!iblWeightBuffer.empty());
	GARDEN_ASSERT(!iblCountBuffer.empty());
	GARDEN_ASSERT(!iblDescriptorSets.empty());
	GARDEN_ASSERT(iblWeightBuffer.size() == iblCountBuffer.size());
	GARDEN_ASSERT(iblWeightBuffer.size() == iblDescriptorSets.size());
	GARDEN_ASSERT(face < Image::cubemapFaceCount);

	Image::CopyImageRegion imageCopyRegion;
	imageCopyRegion.layerCount = Image::cubemapFaceCount;
	Image::copy(ID<Image>(skybox), ID<Image>(specular), imageCopyRegion);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto skyboxView = graphicsSystem->get(skybox);
	auto cubemapSize = skyboxView->getSize().getX() / 2;
	auto pipelineView = graphicsSystem->get(iblSpecularPipeline);
	auto faceCount = face < 0 ? Image::cubemapFaceCount : 1;

	SET_GPU_DEBUG_LABEL("Generate IBL Specular");
	pipelineView->bind();
	
	PbrLightingSystem::SpecularPC pc;
	pc.sampleOffset = 0;
	pc.faceOffset = max(face, (int8)0);

	for (uint8 i = 0; i < (uint8)iblCountBuffer.size(); i++)
	{
		pipelineView->bindDescriptorSet(iblDescriptorSets[i]);

		pc.imageSize = cubemapSize;
		pc.sampleCount = iblCountBuffer[i];
		pc.weight = iblWeightBuffer[i];
		pipelineView->pushConstants(&pc);

		// TODO: Maybe there are faster approaches for IBL specular evaluation. Check Unreal's approach.
		pipelineView->dispatch(uint3(cubemapSize, cubemapSize, faceCount));
		pc.sampleOffset += pc.sampleCount; cubemapSize /= 2;
	}
}

//**********************************************************************************************************************
void PbrLightingSystem::processIblSH(f32x4* shBuffer) noexcept
{
	GARDEN_ASSERT(shBuffer);
	applyShKi(shBuffer);
	deringSH(shBuffer);
	shaderPreprocessSH(shBuffer);
}

//**********************************************************************************************************************
void PbrLightingSystem::generateIblSH(const float4* const* skyboxFaces, uint32 skyboxSize, vector<f32x4>& shBuffer)
{
	GARDEN_ASSERT(skyboxFaces);
	GARDEN_ASSERT(skyboxSize > 0);

	auto threadSystem = ThreadSystem::Instance::tryGet();
	uint32 bufferCount;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		bufferCount = threadPool.getThreadCount();
		shBuffer.resize(bufferCount * shCoeffCount);
		auto shBufferData = shBuffer.data();

		threadPool.addItems([shBufferData, skyboxFaces, skyboxSize](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("IBL SH Generate");
			calcIblSH(shBufferData, skyboxFaces, skyboxSize, 
				task.getTaskIndex(), task.getItemOffset(), task.getItemCount());
		},
		skyboxSize * skyboxSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("IBL SH Generate");
		bufferCount = 1; shBuffer.resize(shCoeffCount);
		calcIblSH(shBuffer.data(), skyboxFaces, skyboxSize, 0, 0, skyboxSize * skyboxSize);
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

	processIblSH(shBufferData);
}

//**********************************************************************************************************************
void PbrLightingSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
	Ref<Buffer>& sh, Ref<Image>& specular, Memory::Strategy strategy, vector<f32x4>* shBuffer)
{
	GARDEN_ASSERT(!path.empty());
	SET_CPU_ZONE_SCOPED("PBR Cubemap Load");
	
	vector<uint8> left, right, bottom, top, back, front; uint2 size;
	ResourceSystem::Instance::get()->loadCubemapData(path, left, right, bottom, top, back, front, size, true);
	auto cubemapSize = size.x;

	auto mipCount = calcMipCount(cubemapSize);
	Image::Mips mips(mipCount);
	mips[0] = { left.data(), right.data(), bottom.data(), top.data(), back.data(), front.data() };

	for (uint8 i = 1; i < mipCount; i++)
		mips[i] = Image::Layers(Image::cubemapFaceCount);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Graphics);
	{
		SET_GPU_DEBUG_LABEL("Load Cubemap PBR");

		vector<f32x4> localSH;
		if (!shBuffer) shBuffer = &localSH;
		generateIblSH((const float4* const*)mips[0].data(), cubemapSize, *shBuffer);

		sh = Ref<Buffer>(graphicsSystem->createBuffer(Buffer::Usage::Uniform | 
			Buffer::Usage::TransferDst, Buffer::CpuAccess::None, shBuffer->data(), 
			shBinarySize, Buffer::Location::PreferGPU, strategy));
		SET_RESOURCE_DEBUG_NAME(sh, "buffer.uniform.sh." + path.generic_string());

		vector<float> iblWeightBuffer; vector<uint32> iblCountBuffer;
		auto specularCache = createSpecularCache(cubemapSize, iblWeightBuffer, iblCountBuffer);

		cubemap = Ref<Image>(graphicsSystem->createCubemap(Image::Format::SfloatR16G16B16A16, 
			Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferSrc, 
			mips, size, strategy, Image::Format::SfloatR32G32B32A32));
		SET_RESOURCE_DEBUG_NAME(cubemap, "image.cubemap." + path.generic_string());

		auto cubemapView = graphicsSystem->get(cubemap);
		cubemapView->generateMips(Sampler::Filter::Linear);		

		auto specularMipCount = calcSpecularMipCount(cubemapSize);
		mips.resize(specularMipCount);
		for (uint8 i = 0; i < specularMipCount; i++)
			mips[i] = Image::Layers(Image::cubemapFaceCount);

		specular = Ref<Image>(graphicsSystem->createCubemap(Image::Format::SfloatR16G16B16A16, Image::Usage::Sampled |  
			Image::Usage::Storage | Image::Usage::TransferDst, mips, uint2(cubemapSize), strategy));
		SET_RESOURCE_DEBUG_NAME(cubemap, "image.cubemap.specular." + path.generic_string());

		vector<ID<ImageView>> specularViews; vector<ID<DescriptorSet>> iblDescriptorSets;
		createIblSpecularViews(ID<Image>(specular), specularViews);
		createIblDescriptorSets(ID<Image>(cubemap), specularCache, specularViews, iblDescriptorSets);

		dispatchIblSpecular(ID<Image>(cubemap), ID<Image>(specular), 
			iblWeightBuffer, iblCountBuffer, iblDescriptorSets);
	
		graphicsSystem->destroy(iblDescriptorSets); graphicsSystem->destroy(specularViews);
		graphicsSystem->destroy(specularCache);
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
Ref<DescriptorSet> PbrLightingSystem::createDescriptorSet(ID<Entity> entity, 
	ID<Pipeline> pipeline, PipelineType type, uint8 index)
{
	GARDEN_ASSERT(entity);

	auto pbrLightingView = tryGetComponent(entity);
	if (!lightingDS || !pbrLightingView || !pbrLightingView->sh || !pbrLightingView->specular)
		return {};

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto shView = graphicsSystem->get(pbrLightingView->sh);
	auto specularView = graphicsSystem->get(pbrLightingView->specular);
	if (!shView->isReady() || !specularView->isReady())
		return {};

	DescriptorSet::Uniforms iblUniforms =
	{ 
		{ "sh", DescriptorSet::Uniform(ID<Buffer>(pbrLightingView->sh)) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
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
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
#include "garden/system/thread.hpp"
#include "garden/profiler.hpp"

#include "math/brdf.hpp"
#include "math/sh.hpp"

#define SPECULAR_SAMPLE_COUNT 1024

using namespace garden;
using namespace math::sh;
using namespace math::ibl;
using namespace math::brdf;

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

#if 0 // Used to precompute Ki coeffs.
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
		// Intentionally left blank
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
	auto nov = clamp((x + 0.5f) / iblDfgSize, 0.0f, 1.0f);
	auto coord = clamp((iblDfgSize - y + 0.5f) / iblDfgSize, 0.0f, 1.0f);
	auto v = f32x4(sqrt(1.0f - nov * nov), 0.0f, nov);
	auto invSampleCount = 1.0f / SPECULAR_SAMPLE_COUNT;
	auto linearRoughness = coord * coord;

	auto r = float2(0.0f);

	for (uint32 i = 0; i < SPECULAR_SAMPLE_COUNT; i++)
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

	return r * (4.0f / SPECULAR_SAMPLE_COUNT);
}

static float mipToLinearRoughness(uint8 lodCount, uint8 mip) noexcept
{
	constexpr auto a = 2.0f, b = -1.0f;
	auto lod = clamp((float)mip / (lodCount - 1), 0.0f, 1.0f);
	auto perceptualRoughness = clamp((sqrt(
		a * a + 4.0f * b * lod) - a) / (2.0f * b), 0.0f, 1.0f);
	return perceptualRoughness * perceptualRoughness;
}

static uint32 calcSampleCount(uint8 mipLevel) noexcept
{
	return SPECULAR_SAMPLE_COUNT * (uint32)exp2((float)std::max(mipLevel - 1, 0));
}

//**********************************************************************************************************************
static ID<Image> createShadowBuffer(ID<ImageView>* shadowImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	Image::Mips mips(1); mips[0].assign(PbrLightingRenderSystem::shadowBufferCount, nullptr);
	auto image = graphicsSystem->createImage(PbrLightingRenderSystem::shadowBufferFormat, Image::Usage::ColorAttachment | 
		Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen, 
		mips, graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.shadowBuffer");

	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount; i++)
	{
		shadowImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, PbrLightingRenderSystem::shadowBufferFormat, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(shadowImageViews[i], "imageView.lighting.shadow" + to_string(i));
	}

	return image;
}
static void destroyShadowBuffer(ID<Image> shadowBuffer, ID<ImageView>* shadowImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowImageViews[i]);
		shadowImageViews[i] = {};
	}
	graphicsSystem->destroy(shadowBuffer);
}

static void createShadowFramebuffers(const ID<ImageView>* shadowImageViews, ID<Framebuffer>* shadowFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	const Framebuffer::OutputAttachment::Flags attachmentFlags = { false, false, true };

	vector<Framebuffer::OutputAttachment> colorAttachments
	{ Framebuffer::OutputAttachment(shadowImageViews[0], attachmentFlags) };
	shadowFramebuffers[0] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(shadowFramebuffers[0], "framebuffer.lighting.shadow0");

	colorAttachments = { Framebuffer::OutputAttachment(shadowImageViews[1], attachmentFlags) };
	shadowFramebuffers[1] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(shadowFramebuffers[1], "framebuffer.lighting.shadow1");

	colorAttachments = { Framebuffer::OutputAttachment(shadowImageViews[0], attachmentFlags) };
	shadowFramebuffers[2] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(shadowFramebuffers[2], "framebuffer.lighting.shadow2");
}
static void destroyShadowFramebuffers(ID<Framebuffer>* shadowFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount + 1; i++)
	{
		graphicsSystem->destroy(shadowFramebuffers[i]);
		shadowFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createAoBuffer(ID<ImageView>* aoImageViews, ID<Image>& aoDenBuffer)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	Image::Mips mips; mips.assign(PbrLightingRenderSystem::aoBufferCount - 1, { nullptr });
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();

	auto image = graphicsSystem->createImage(PbrLightingRenderSystem::aoBufferFormat, 
		usage, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.aoBuffer");
	aoDenBuffer = graphicsSystem->createImage(PbrLightingRenderSystem::aoBufferFormat, 
		usage, { { nullptr } }, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(aoDenBuffer, "image.lighting.aoDenBuffer");

	auto denBufferView = graphicsSystem->get(aoDenBuffer);
	aoImageViews[0] = denBufferView->getDefaultView();
	SET_RESOURCE_DEBUG_NAME(aoImageViews[0], "imageView.lighting.aoDen");

	for (uint32 i = 1; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		aoImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, PbrLightingRenderSystem::aoBufferFormat, i - 1, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(aoImageViews[i], "imageView.lighting.ao" + to_string(i));
	}
	return image;
}
static void destroyAoBuffer(ID<Image> aoBuffer, ID<ImageView>* aoImageViews, ID<Image> aoDenBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();

	aoImageViews[0] = {};
	for (uint32 i = 1; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoImageViews[i]);
		aoImageViews[i] = {};
	}

	graphicsSystem->destroy(aoDenBuffer);
	graphicsSystem->destroy(aoBuffer);
}

static void createAoFramebuffers(const ID<ImageView>* aoImageViews, ID<Framebuffer>* aoFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	const Framebuffer::OutputAttachment::Flags attachmentFlags = { false, false, true };

	vector<Framebuffer::OutputAttachment> colorAttachments
	{ Framebuffer::OutputAttachment(aoImageViews[0], attachmentFlags) };
	aoFramebuffers[0] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[0], "framebuffer.lighting.aoDen");

	colorAttachments = { Framebuffer::OutputAttachment(aoImageViews[1], attachmentFlags) };
	aoFramebuffers[1] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[1], "framebuffer.lighting.ao1");

	framebufferSize = max(framebufferSize / 2u, uint2::one);
	colorAttachments = { Framebuffer::OutputAttachment(aoImageViews[2], attachmentFlags) };
	aoFramebuffers[2] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(aoFramebuffers[2], "framebuffer.lighting.ao2");
}
static void destroyAoFramebuffers(ID<Framebuffer>* aoFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoFramebuffers[i]);
		aoFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createReflectionBuffer(vector<ID<ImageView>>& reflImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto reflBufferSize = graphicsSystem->getScaledFramebufferSize();
	auto mipCount = calcMipCount(reflBufferSize);
	reflImageViews.resize(mipCount);

	Image::Mips mips(mipCount);
	for (uint8 i = 0; i < mipCount; i++)
		mips[i].push_back(nullptr);

	auto image = graphicsSystem->createImage(PbrLightingRenderSystem::reflBufferFormat, 
		Image::Usage::ColorAttachment | Image::Usage::Sampled | Image::Usage::Storage | Image::Usage::TransferDst | 
		Image::Usage::Fullscreen, mips, reflBufferSize, Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.refBuffer");

	for (uint8 i = 0; i < mipCount; i++)
	{
		auto imageView = graphicsSystem->createImageView(image, 
			Image::Type::Texture2D, PbrLightingRenderSystem::reflBufferFormat, i, 1, 0, 1);
		SET_RESOURCE_DEBUG_NAME(imageView, "imageView.lighting.reflBuffer" + to_string(i));
		reflImageViews[i] = imageView;
	}
	return image;
}
static void createReflFramebuffers(const vector<ID<ImageView>>& reflImageViews, 
	vector<ID<Framebuffer>>& reflFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	auto mipCount = (uint8)reflImageViews.size();
	reflFramebuffers.resize(mipCount);

	for (uint8 i = 0; i < mipCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments =
		{ Framebuffer::OutputAttachment(reflImageViews[i], { false, false, true }) };
		auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.lighting.reflection" + to_string(i));
		reflFramebuffers[i] = framebuffer;
		framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getLightingUniforms(ID<Image> dfgLUT, const ID<ImageView>* shadowImageViews, 
	const ID<ImageView>* aoImageViews, ID<Image> reflectionBuffer)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());
	auto reflBufferView = reflectionBuffer ? 
		graphicsSystem->get(reflectionBuffer)->getDefaultView() : graphicsSystem->getEmptyTexture();
	const auto& colorAttachments = gFramebufferView->getColorAttachments();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(gFramebufferView->getDepthStencilAttachment().imageView) },
		{ "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0] ?
			shadowImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "aoBuffer", DescriptorSet::Uniform(aoImageViews[0] ?
			aoImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "reflBuffer", DescriptorSet::Uniform(reflBufferView) },
		{ "dfgLUT", DescriptorSet::Uniform(graphicsSystem->get(dfgLUT)->getDefaultView()) }
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(colorAttachments[i].imageView ? 
			colorAttachments[i].imageView : graphicsSystem->getEmptyTexture()));
	}

	return uniforms;
}

static ID<GraphicsPipeline> createLightingPipeline(bool useShadowBuffer, bool useAoBuffer, bool useReflBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	Pipeline::SpecConstValues specConstValues =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConstValue(useShadowBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConstValue(useAoBuffer) },
		{ "USE_REFLECTION_BUFFER", Pipeline::SpecConstValue(useReflBuffer) },
		{ "USE_EMISSIVE_BUFFER", Pipeline::SpecConstValue(deferredSystem->useEmissive()) },
		{ "USE_GI_BUFFER", Pipeline::SpecConstValue(deferredSystem->useGI()) },
	};

	return ResourceSystem::Instance::get()->loadGraphicsPipeline("pbr-lighting", 
		deferredSystem->getHdrFramebuffer(), deferredSystem->useAsyncRecording(), true, 0, 0, &specConstValues);
}
static ID<ComputePipeline> createIblSpecularPipeline()
{
	return ResourceSystem::Instance::get()->loadComputePipeline("ibl-specular", false, false);
}

//**********************************************************************************************************************
static ID<Image> createDfgLUT()
{
	vector<float2> pixels(iblDfgSize * iblDfgSize);
	auto pixelData = pixels.data();

	// TODO: check if release build DFG image looks the same as debug.
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

//**********************************************************************************************************************
bool PbrLightingRenderComponent::destroy()
{
	if (!entity)
		return false;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (cubemap.isLastRef())
		graphicsSystem->destroy(ID<Image>(cubemap));
	if (sh.isLastRef())
		graphicsSystem->destroy(ID<Buffer>(sh));
	if (specular.isLastRef())
		graphicsSystem->destroy(ID<Image>(specular));
	if (descriptorSet.isLastRef())
		graphicsSystem->destroy(ID<DescriptorSet>(descriptorSet));
	return true;
}

PbrLightingRenderSystem::PbrLightingRenderSystem(bool useShadowBuffer, 
	bool useAoBuffer, bool useReflBuffer, bool setSingleton) : Singleton(setSingleton), 
	hasShadowBuffer(useShadowBuffer), hasAoBuffer(useAoBuffer), hasReflBuffer(useReflBuffer)
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

	ECSM_SUBSCRIBE_TO_EVENT("Init", PbrLightingRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PbrLightingRenderSystem::deinit);
}
PbrLightingRenderSystem::~PbrLightingRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingRenderSystem::deinit);

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
	}
	else
	{
		components.clear(false);
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreHdrRender", PbrLightingRenderSystem::preHdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("HdrRender", PbrLightingRenderSystem::hdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("GBufferRecreate", PbrLightingRenderSystem::gBufferRecreate);

	if (!dfgLUT)
		dfgLUT = createDfgLUT();

	if (hasShadowBuffer)
	{
		if (!shadowBuffer)
			shadowBuffer = createShadowBuffer(shadowImageViews);
		if (!shadowFramebuffers[0])
			createShadowFramebuffers(shadowImageViews, shadowFramebuffers);
	}
	if (hasAoBuffer)
	{
		if (!aoBuffer)
			aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
		if (!aoFramebuffers[0])
			createAoFramebuffers(aoImageViews, aoFramebuffers);
	}
	if (hasReflBuffer)
	{
		if (!reflectionBuffer)
			reflectionBuffer = createReflectionBuffer(reflImageViews);
		if (reflFramebuffers.empty())
			createReflFramebuffers(reflImageViews, reflFramebuffers);
	}

	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(hasShadowBuffer, hasAoBuffer, hasReflBuffer);
}
void PbrLightingRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		// TODO: graphicsSystem->destroy(reflDenoiseDS);
		graphicsSystem->destroy(aoDenoiseDS);
		graphicsSystem->destroy(shadowDenoiseDS);
		graphicsSystem->destroy(lightingDS);
		graphicsSystem->destroy(iblSpecularPipeline);
		graphicsSystem->destroy(lightingPipeline);
		graphicsSystem->destroy(reflFramebuffers);
		graphicsSystem->destroy(reflImageViews);
		graphicsSystem->destroy(reflectionBuffer);
		destroyAoFramebuffers(aoFramebuffers);
		destroyAoBuffer(aoBuffer, aoImageViews, aoDenBuffer);
		destroyShadowFramebuffers(aoFramebuffers);
		destroyShadowBuffer(shadowBuffer, shadowImageViews);
		graphicsSystem->destroy(dfgLUT);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreHdrRender", PbrLightingRenderSystem::preHdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("HdrRender", PbrLightingRenderSystem::hdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("GBufferRecreate", PbrLightingRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::preHdrRender()
{
	SET_CPU_ZONE_SCOPED("PBR Lighting Pre HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	if (pipelineView->isReady() && !lightingDS)
	{
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews, reflectionBuffer);
		lightingDS = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDS, "descriptorSet.lighting.base");
	}

	auto manager = Manager::Instance::get();
	if (hasReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre Refl Render");

		auto event = &manager->getEvent("PreReflRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (hasShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre Shadow Render");

		auto event = &manager->getEvent("PreShadowRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (hasAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre AO Render");

		auto event = &manager->getEvent("PreAoRender");
		if (event->hasSubscribers())
			event->run();
	}

	if (hasReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Refl Render Pass");

		auto event = &manager->getEvent("ReflRender");
		if (event->hasSubscribers())
		{
			if (!reflectionBuffer)
				reflectionBuffer = createReflectionBuffer(reflImageViews);
			if (reflFramebuffers.empty())
				createReflFramebuffers(reflImageViews, reflFramebuffers);

			auto framebufferView = graphicsSystem->get(reflFramebuffers[0]);
			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Reflection Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::zero);
				event->run();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
	if (hasShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Shadow Render Pass");

		auto event = &manager->getEvent("ShadowRender");
		if (event->hasSubscribers())
		{
			if (!shadowBuffer)
				shadowBuffer = createShadowBuffer(shadowImageViews);
			if (!shadowFramebuffers[0])
				createShadowFramebuffers(shadowImageViews, shadowFramebuffers);

			auto framebufferView = graphicsSystem->get(shadowFramebuffers[0]);
			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::one);
				event->run();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}
	if (hasAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("AO Render Pass");

		auto event = &manager->getEvent("AoRender");
		if (event->hasSubscribers())
		{
			if (!aoBuffer)
				aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
			if (!aoFramebuffers[0])
				createAoFramebuffers(aoImageViews, aoFramebuffers);

			auto framebufferView = graphicsSystem->get(aoFramebuffers[2]);
			graphicsSystem->startRecording(CommandBufferType::Frame);
			{
				SET_GPU_DEBUG_LABEL("AO Pass", Color::transparent);
				framebufferView->beginRenderPass(float4::one);
				event->run();
				framebufferView->endRenderPass();
			}
			graphicsSystem->stopRecording();
		}
	}

	if (hasReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post Refl Render");

		auto event = &manager->getEvent("PostReflRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (hasShadowBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post Shadow Render");

		auto event = &manager->getEvent("PostShadowRender");
		if (event->hasSubscribers())
			event->run();
	}
	if (hasAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post AO Render");

		auto event = &manager->getEvent("PostAoRender");
		if (event->hasSubscribers())
			event->run();
	}

	graphicsSystem->startRecording(CommandBufferType::Frame);

	if (hasAnyRefl) // Note: reflections go first
	{
		// TODO: downsample reflection buffer based on roughness.
		hasAnyRefl = false;
	}
	else if (reflectionBuffer)
	{
		auto imageView = graphicsSystem->get(reflectionBuffer);
		imageView->clear(float4::zero);
	}

	if (hasAnyShadow)
	{
		GpuProcessSystem::Instance::get()->bilateralBlurD(shadowImageViews[0], shadowFramebuffers[2],
			shadowImageViews[1], shadowFramebuffers[1], float2(1.0f), denoiseSharpness, shadowDenoiseDS);
		hasAnyShadow = false;
	}
	else if (shadowBuffer)
	{
		auto imageView = graphicsSystem->get(shadowBuffer);
		imageView->clear(float4::one);
	}

	if (hasAnyAO)
	{
		GpuProcessSystem::Instance::get()->bilateralBlurD(aoImageViews[2], aoFramebuffers[0],
			aoImageViews[1], aoFramebuffers[1], float2(1.0f), denoiseSharpness, aoDenoiseDS);
		hasAnyAO = false;
	}
	else if (aoBuffer)
	{
		auto imageView = graphicsSystem->get(aoBuffer);
		imageView->clear(float4::one);
		imageView = graphicsSystem->get(aoDenBuffer);
		imageView->clear(float4::one);
	}

	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::hdrRender()
{
	SET_CPU_ZONE_SCOPED("PBR Lighting HDR Render");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	auto dfgLutView = graphicsSystem->get(dfgLUT);
	if (!pipelineView->isReady() || !dfgLutView->isReady() || !lightingDS)
		return;

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(graphicsSystem->camera);
	if (transformView && !transformView->isActive())
		return;

	auto pbrLightingView = tryGetComponent(graphicsSystem->camera);
	if (!pbrLightingView || !pbrLightingView->cubemap || !pbrLightingView->sh || !pbrLightingView->specular)
		return;

	auto cubemapView = graphicsSystem->get(pbrLightingView->cubemap);
	auto shView = graphicsSystem->get(pbrLightingView->sh);
	auto specularView = graphicsSystem->get(pbrLightingView->specular);
	if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
		return;
	
	if (!pbrLightingView->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet( // TODO: maybe create shared DS?
			ID<Buffer>(pbrLightingView->sh), ID<Image>(pbrLightingView->specular));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.lighting" + to_string(*descriptorSet));
		pbrLightingView->descriptorSet = descriptorSet;
	}

	const auto& cameraConstants = graphicsSystem->getCameraConstants();
	static const auto uvToNDC = f32x4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	DescriptorSet::Range descriptorSetRange[2];
	descriptorSetRange[0] = DescriptorSet::Range(lightingDS);
	descriptorSetRange[1] = DescriptorSet::Range(ID<DescriptorSet>(pbrLightingView->descriptorSet));

	LightingPC pc;
	pc.uvToWorld = (float4x4)(cameraConstants.invViewProj * uvToNDC);
	pc.shadow = (float4)cameraConstants.shadowColor;
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
void PbrLightingRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();

	if (reflectionBuffer)
	{
		graphicsSystem->destroy(reflImageViews);
		graphicsSystem->destroy(reflectionBuffer);
		reflectionBuffer = createReflectionBuffer(reflImageViews);
	}
	if (!reflFramebuffers.empty())
	{
		graphicsSystem->destroy(reflFramebuffers);
		createReflFramebuffers(reflImageViews, reflFramebuffers);
	}
	
	if (aoBuffer)
	{
		destroyAoBuffer(aoBuffer, aoImageViews, aoDenBuffer);
		aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
	}
	if (aoFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorAttachment = Framebuffer::OutputAttachment(aoImageViews[0], { false, false, true });
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
		destroyShadowBuffer(shadowBuffer, shadowImageViews);
		shadowBuffer = createShadowBuffer(shadowImageViews);
	}
	if (shadowFramebuffers[0])
	{
		auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
		auto colorAttachment = Framebuffer::OutputAttachment(shadowImageViews[0], { false, false, true });
		auto framebufferView = graphicsSystem->get(shadowFramebuffers[0]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		colorAttachment.imageView = shadowImageViews[1];
		framebufferView = graphicsSystem->get(shadowFramebuffers[1]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);

		colorAttachment.imageView = shadowImageViews[0];
		framebufferView = graphicsSystem->get(shadowFramebuffers[2]);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}

	if (lightingDS)
	{
		graphicsSystem->destroy(lightingDS);
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews, reflectionBuffer);
		lightingDS = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDS, "descriptorSet.lighting.base");
	}

	if (shadowDenoiseDS)
	{
		graphicsSystem->destroy(shadowDenoiseDS);
		shadowDenoiseDS = {};
	}
	if (aoDenoiseDS)
	{
		graphicsSystem->destroy(aoDenoiseDS);
		aoDenoiseDS = {};
	}
	/** TODO: destroy downsample descriptor sets.
	if (reflDenoiseDS)
	{
		graphicsSystem->destroy(reflDenoiseDS);
		reflDenoiseDS = {};
	}
	*/

	if (aoBuffer || aoFramebuffers[0])
		Manager::Instance::get()->runEvent("AoRecreate");
	if (shadowBuffer || shadowFramebuffers[0])
		Manager::Instance::get()->runEvent("ShadowRecreate");
	if (reflectionBuffer || !reflFramebuffers.empty())
		Manager::Instance::get()->runEvent("ReflRecreate");
}

//**********************************************************************************************************************
string_view PbrLightingRenderSystem::getComponentName() const
{
	return "PBR Lighting";
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::setConsts(bool useShadowBuffer, bool useAoBuffer, bool useReflBuffer)
{
	if (this->hasShadowBuffer == useShadowBuffer && this->hasAoBuffer == useAoBuffer)
		return;

	this->hasShadowBuffer = useShadowBuffer;
	this->hasAoBuffer = useAoBuffer;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(lightingDS);
	lightingDS = {};

	for (auto& pbrLighting : components)
	{
		if (!pbrLighting.getEntity())
			continue;
		if (pbrLighting.descriptorSet.isLastRef())
			graphicsSystem->destroy(ID<DescriptorSet>(pbrLighting.descriptorSet));
		pbrLighting.descriptorSet = {};
	}

	if (this->hasShadowBuffer != useShadowBuffer)
	{
		if (useShadowBuffer)
		{
			shadowBuffer = createShadowBuffer(shadowImageViews);
			createShadowFramebuffers(shadowImageViews, shadowFramebuffers);
		}
		else
		{
			destroyShadowFramebuffers(shadowFramebuffers);
			destroyShadowBuffer(shadowBuffer, shadowImageViews);
			shadowBuffer = {};
		}
	}
	if (this->hasAoBuffer != useAoBuffer)
	{
		if (useAoBuffer)
		{
			aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
			createAoFramebuffers(aoImageViews, aoFramebuffers);
		}
		else
		{
			destroyAoFramebuffers(aoFramebuffers);
			destroyAoBuffer(aoBuffer, aoImageViews, aoDenBuffer);
			aoBuffer = {};
		}
	}
	if (this->hasReflBuffer != useReflBuffer)
	{
		if (useReflBuffer)
		{
			reflectionBuffer = createReflectionBuffer(reflImageViews);
			createReflFramebuffers(reflImageViews, reflFramebuffers);
		}
		else
		{
			graphicsSystem->destroy(reflFramebuffers);
			graphicsSystem->destroy(reflImageViews);
			graphicsSystem->destroy(reflectionBuffer);
			reflectionBuffer = {}; reflImageViews = {}; reflFramebuffers = {};
		}
	}

	if (lightingPipeline)
	{
		graphicsSystem->destroy(lightingPipeline);
		lightingPipeline = createLightingPipeline(useShadowBuffer, useAoBuffer, useReflBuffer);
	}
}

//**********************************************************************************************************************
ID<GraphicsPipeline> PbrLightingRenderSystem::getLightingPipeline()
{
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(hasShadowBuffer, hasAoBuffer, hasReflBuffer);
	return lightingPipeline;
}
ID<ComputePipeline> PbrLightingRenderSystem::getIblSpecularPipeline()
{
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();
	return iblSpecularPipeline;
}

const ID<Framebuffer>* PbrLightingRenderSystem::getShadowFramebuffers()
{
	if (!shadowFramebuffers[0])
		createShadowFramebuffers(getShadowImageViews(), shadowFramebuffers);
	return shadowFramebuffers;
}
const ID<Framebuffer>* PbrLightingRenderSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0])
		createAoFramebuffers(getAoImageViews(), aoFramebuffers);
	return aoFramebuffers;
}
const vector<ID<Framebuffer>>& PbrLightingRenderSystem::getReflFramebuffers()
{
	if (reflFramebuffers.empty())
		createReflFramebuffers(getReflImageViews(), reflFramebuffers);
	return reflFramebuffers;
}

//**********************************************************************************************************************
ID<Image> PbrLightingRenderSystem::getDfgLUT()
{
	if (!dfgLUT)
		dfgLUT = createDfgLUT();
	return dfgLUT;
}

ID<Image> PbrLightingRenderSystem::getShadowBuffer()
{
	if (!shadowBuffer && hasShadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowBuffer;
}
ID<Image> PbrLightingRenderSystem::getAoBuffer()
{
	if (!aoBuffer && hasAoBuffer)
		aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
	return aoBuffer;
}
ID<Image> PbrLightingRenderSystem::getAoDenBuffer()
{
	if (!aoBuffer && hasAoBuffer)
		aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
	return aoDenBuffer;
}
ID<Image> PbrLightingRenderSystem::getReflBuffer()
{
	if (!reflectionBuffer && hasReflBuffer)
		reflectionBuffer = createReflectionBuffer(reflImageViews);
	return reflectionBuffer;
}

const ID<ImageView>* PbrLightingRenderSystem::getShadowImageViews()
{
	if (!shadowBuffer && hasShadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowImageViews;
}
const ID<ImageView>* PbrLightingRenderSystem::getAoImageViews()
{
	if (!aoBuffer && hasAoBuffer)
		aoBuffer = createAoBuffer(aoImageViews, aoDenBuffer);
	return aoImageViews;
}
const vector<ID<ImageView>>& PbrLightingRenderSystem::getReflImageViews()
{
	if (!reflectionBuffer && hasReflBuffer)
		reflectionBuffer = createReflectionBuffer(reflImageViews);
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

		PbrLightingRenderSystem::SpecularPC pc;
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
void PbrLightingRenderSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
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
Ref<DescriptorSet> PbrLightingRenderSystem::createDescriptorSet(ID<Buffer> sh, 
	ID<Image> specular, ID<GraphicsPipeline> pipeline, uint8 index)
{
	GARDEN_ASSERT(sh);
	GARDEN_ASSERT(specular);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto specularView = graphicsSystem->get(specular);

	DescriptorSet::Uniforms iblUniforms =
	{ 
		{ "sh", DescriptorSet::Uniform(sh) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
	};

	auto descriptorSet = graphicsSystem->createDescriptorSet(pipeline ? 
		pipeline : lightingPipeline, std::move(iblUniforms), {}, index);
	return Ref<DescriptorSet>(descriptorSet);
}
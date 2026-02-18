// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
		SpecularData() noexcept = default;
	};
}

#if 0 // Note: Used to precompute Ki coeffs.
#include <iostream>

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
	auto k = (l * 2 + 1) * factorial(l - m, l + m);
	return sqrt(k) * (M_2_SQRTPI * 0.25);
}
static double computeTruncatedCosSh(uint32 l) noexcept
{
	if (l == 0) return M_PI;
	else if (l == 1) return 2.0 * M_PI / 3.0;
	else if (l & 1u) return 0.0;

	auto l2 = l / 2;
	auto a0 = ((l2 & 1u) ? 1.0 : -1.0) / ((l + 2) * (l - 1));
	auto a1 = factorial(l, l2) / (factorial(l2) * (1u << l));
	return a0 * a1 * (M_PI * 2.0);
}

//**********************************************************************************************************************
static void computeKi(int32 bandCount) noexcept
{
	vector<double> ki(bandCount * bandCount);

	for (uint32 l = 0; l < bandCount; l++)
	{
		ki[shIndex(0, l)] = computeKml(0, l);

		for (uint32 m = 1; m <= l; m++)
			ki[shIndex(m, l)] = ki[shIndex(-m, l)] = computeKml(m, l) * M_SQRT2;
	}

	for (uint32 l = 0; l < bandCount; l++)
	{
		auto truncatedCosSh = computeTruncatedCosSh(l);
		ki[shIndex(0, l)] *= truncatedCosSh;

		for (uint32 m = 1; m <= l; m++)
		{
			ki[shIndex(-m, l)] *= truncatedCosSh;
			ki[shIndex( m, l)] *= truncatedCosSh;
		}
	}

	for (uint32 i = 0; i < sh3Count; i++)
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

static float smithGGXCorrelated(float nov, float nol, float linearRoughness) noexcept
{
	auto a2 = linearRoughness * linearRoughness;
	auto lambdaV = nol * sqrt(fma(nov - nov * a2, nov, a2));
	auto lambdaL = nov * sqrt(fma(nol - nol * a2, nol, a2));
	return 0.5f / (lambdaV + lambdaL);
}
static float2 dfvMultiscatter(uint32 x, uint32 y, uint32 dfgSize, uint32 sampleCount) noexcept
{
	auto invSampleCount = 1.0f / sampleCount;
	auto nov = saturate((x + 0.5f) / dfgSize);
	auto coord = saturate((dfgSize - y + 0.5f) / dfgSize);
	auto v = f32x4(sqrt(1.0f - nov * nov), 0.0f, nov);
	auto linearRoughness = coord * coord;

	auto r = float2(0.0f);
	for (uint32 i = 0; i < sampleCount; i++)
	{
		auto u = hammersley(i, invSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto voh = dot3(v, h); auto l = voh * h * 2.0f - v; voh = saturate(voh);
		auto nol = saturate(l.getZ()), noh = saturate(h.getZ());

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
	auto lod = saturate((float)mip / ((int)lodCount - 1));
	auto perceptualRoughness = saturate((sqrt(fma(
		lod, 4.0f * b, a * a)) - a) / (b * 2.0f));
	return perceptualRoughness * perceptualRoughness;
}

static uint32 calcIblSampleCount(uint8 mipLevel, uint32 sampleCount) noexcept
{
	return sampleCount * (uint32)exp2((float)std::max((int)mipLevel - 1, 0));
}

//**********************************************************************************************************************
static void createProcBuffers(GraphicsSystem* graphicsSystem, ID<Image>& baseBuffer, 
	ID<Image>& blurBuffer, Image::Format format, const char* debugName)
{
	constexpr auto usage = Image::Usage::ColorAttachment | Image::Usage::Sampled | 
		Image::Usage::Storage | Image::Usage::TransferDst | Image::Usage::Fullscreen;
	constexpr auto strategy = Image::Strategy::Size;

	Image::Mips mips; mips.assign(PbrLightingSystem::procBufferCount - 1, { nullptr });
	auto framebufferSize = graphicsSystem->getScaledFrameSize();

	baseBuffer = graphicsSystem->createImage(format, usage, mips, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(baseBuffer, "image.pbrLighting." + string(debugName) + ".base");
	blurBuffer = graphicsSystem->createImage(format, usage, { { nullptr } }, framebufferSize, strategy);
	SET_RESOURCE_DEBUG_NAME(blurBuffer, "image.pbrLighting." + string(debugName) + ".blur");
}

static void createProcFramebuffers(GraphicsSystem* graphicsSystem, ID<Image> baseBuffer, 
	ID<Image> blurBuffer, ID<Framebuffer>* framebuffers, const char* debugName)
{
	auto baseBufferView = graphicsSystem->get(baseBuffer);
	auto blurBufferView = graphicsSystem->get(blurBuffer);
	auto framebufferSize = graphicsSystem->getScaledFrameSize();

	vector<Framebuffer::OutputAttachment> colorAttachments
	{ Framebuffer::OutputAttachment(blurBufferView->getView(), PbrLightingSystem::procFbFlags) };
	framebuffers[PbrLightingSystem::blurProcIndex] = 
		graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[PbrLightingSystem::blurProcIndex], 
		"framebuffer.pbrLighting." + string(debugName) + ".blur");

	colorAttachments = { Framebuffer::OutputAttachment(
		baseBufferView->getView(0, 0), PbrLightingSystem::procFbFlags) };
	framebuffers[PbrLightingSystem::tempProcIndex] = 
		graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[PbrLightingSystem::tempProcIndex], 
		"framebuffer.pbrLighting." + string(debugName) + ".temp");

	framebufferSize = max(framebufferSize / 2u, uint2::one);
	colorAttachments = { Framebuffer::OutputAttachment(
		baseBufferView->getView(0, 1), PbrLightingSystem::procFbFlags) };
	framebuffers[PbrLightingSystem::baseProcIndex] = 
		graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffers[PbrLightingSystem::baseProcIndex], 
		"framebuffer.pbrLighting." + string(debugName) + ".base");
}
static void updateProcFramebuffer(GraphicsSystem* graphicsSystem, 
	ID<Image> baseBuffer, ID<Image> blurBuffer, ID<Framebuffer>* framebuffers)
{
	auto baseBufferView = graphicsSystem->get(baseBuffer);
	auto blurBufferView = graphicsSystem->get(blurBuffer);
	auto framebufferSize = graphicsSystem->getScaledFrameSize();

	auto colorAttachment = Framebuffer::OutputAttachment(blurBufferView->getView(), PbrLightingSystem::procFbFlags);
	auto framebufferView = graphicsSystem->get(framebuffers[PbrLightingSystem::blurProcIndex]);
	framebufferView->update(framebufferSize, &colorAttachment, 1);

	colorAttachment = Framebuffer::OutputAttachment(baseBufferView->getView(0, 0), PbrLightingSystem::procFbFlags);
	framebufferView = graphicsSystem->get(framebuffers[PbrLightingSystem::tempProcIndex]);
	framebufferView->update(framebufferSize, &colorAttachment, 1);

	framebufferSize = max(framebufferSize / 2u, uint2::one);
	colorAttachment = Framebuffer::OutputAttachment(baseBufferView->getView(0, 1), PbrLightingSystem::procFbFlags);
	framebufferView = graphicsSystem->get(framebuffers[PbrLightingSystem::baseProcIndex]);
	framebufferView->update(framebufferSize, &colorAttachment, 1);
}

//**********************************************************************************************************************
static ID<Image> createReflBuffer(GraphicsSystem* graphicsSystem, bool useBlur)
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
	return image;
}
static ID<Framebuffer> createReflFramebuffer(GraphicsSystem* graphicsSystem, ID<Image> reflBuffer)
{
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto reflBufferView = graphicsSystem->get(reflBuffer)->getView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(reflBufferView, PbrLightingSystem::procFbFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.pbrLighting.reflections");
	return framebuffer;
}

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
	auto framebufferSize = graphicsSystem->getScaledFrameSize();
	auto giBufferView = graphicsSystem->get(giBuffer)->getView();
	vector<Framebuffer::OutputAttachment> colorAttachments =
	{ Framebuffer::OutputAttachment(giBufferView, PbrLightingSystem::procFbFlags) };
	auto framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
	SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.pbrLighting.gi");
	return framebuffer;
}

//**********************************************************************************************************************
static DescriptorSet::Uniforms getLightingUniforms(GraphicsSystem* graphicsSystem, ID<Image> dfgLUT, 
	ID<Image> shadBlurBuffer, ID<Image> aoBlurBuffer, ID<Image> reflBuffer, ID<Image> giBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto depthBufferView = deferredSystem->getDepthImageView();
	auto shadBlurView = shadBlurBuffer ? graphicsSystem->get(shadBlurBuffer)->getView() : ID<ImageView>();
	auto aoBlurView = aoBlurBuffer ? graphicsSystem->get(aoBlurBuffer)->getView() : ID<ImageView>();
	auto reflBufferView = reflBuffer ? graphicsSystem->get(reflBuffer)->getView() : ID<ImageView>();
	auto giBufferView = giBuffer ? graphicsSystem->get(giBuffer)->getView() : ID<ImageView>();
	auto dfgLutView = graphicsSystem->get(dfgLUT)->getView();
	const auto& gColorAttachments = gFramebufferView->getColorAttachments();
	
	graphicsSystem->startRecording(CommandBufferType::Frame);
	auto emptyView = graphicsSystem->getEmptyTexture();
	auto whiteView = graphicsSystem->getWhiteTexture();
	graphicsSystem->stopRecording();

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthBufferView) },
		{ "shadBuffer", DescriptorSet::Uniform(shadBlurView ? shadBlurView : whiteView) },
		{ "aoBuffer", DescriptorSet::Uniform(aoBlurView ? aoBlurView : whiteView) },
		{ "reflBuffer", DescriptorSet::Uniform(reflBufferView ? reflBufferView : emptyView) },
		{ "giBuffer", DescriptorSet::Uniform(giBufferView ? giBufferView : emptyView) },
		{ "dfgLUT", DescriptorSet::Uniform(dfgLutView) }
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
	{
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(
			gColorAttachments[i].imageView ? gColorAttachments[i].imageView : emptyView));
	}

	return uniforms;
}

static ID<GraphicsPipeline> createLightingPipeline(PbrLightingSystem::Options pbrOptions)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	Pipeline::SpecConstValues specConstValues =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConstValue(pbrOptions.useShadBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConstValue(pbrOptions.useAoBuffer) },
		{ "USE_REFLECTION_BUFFER", Pipeline::SpecConstValue(pbrOptions.useReflBuffer) },
		{ "USE_REFLECTION_BLUR", Pipeline::SpecConstValue(pbrOptions.useReflBlur) },
		{ "USE_GI_BUFFER", Pipeline::SpecConstValue(pbrOptions.useGiBuffer) },
		{ "USE_CLEAR_COAT_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useClearCoat) },
		{ "USE_EMISSION_BUFFER", Pipeline::SpecConstValue(deferredSystem->getOptions().useEmission) }
	};

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConstValues;
	options.useAsyncRecording = deferredSystem->getOptions().useAsyncRecording;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"pbr-lighting", deferredSystem->getHdrFramebuffer(), options);
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
		case GraphicsQuality::PotatoPC: return 128;
		case GraphicsQuality::Low: return 128;
		case GraphicsQuality::Medium: return 256;
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
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingSystem::deinit);

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
	auto manager = Manager::Instance::get();
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
	if (!shadBaseBuffer)
		createProcBuffers(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadBufferFormat, "shad");
	if (!shadFramebuffers[0])
		createProcFramebuffers(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadFramebuffers, "shad");
	if (!aoBaseBuffer)
		createProcBuffers(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoBufferFormat, "ao");
	if (!aoFramebuffers[0])
		createProcFramebuffers(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoFramebuffers, "ao");
	if (!reflBuffer)
		reflBuffer = createReflBuffer(graphicsSystem, options.useReflBlur);
	if (!options.useReflBlur && !reflFramebuffer)
		reflFramebuffer = createReflFramebuffer(graphicsSystem, reflBuffer);
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
		graphicsSystem->destroy(shadBlurDS);
		graphicsSystem->destroy(lightingDS);
		graphicsSystem->destroy(reflBlurPipeline);
		graphicsSystem->destroy(aoBlurPipeline);
		graphicsSystem->destroy(shadBlurPipeline);
		graphicsSystem->destroy(iblSpecularPipeline);
		graphicsSystem->destroy(lightingPipeline);
		graphicsSystem->destroy(reflFramebuffers);
		graphicsSystem->destroy(giFramebuffer);
		graphicsSystem->destroy(reflFramebuffer);
		graphicsSystem->destroy(giBuffer);
		graphicsSystem->destroy(reflBuffer);
		graphicsSystem->destroy(aoFramebuffers, procBufferCount);
		graphicsSystem->destroy(aoBlurBuffer);
		graphicsSystem->destroy(aoBaseBuffer);
		graphicsSystem->destroy(shadFramebuffers, procBufferCount);
		graphicsSystem->destroy(shadBlurBuffer);
		graphicsSystem->destroy(shadBaseBuffer);
		graphicsSystem->destroy(dfgLUT);

		auto manager = Manager::Instance::get();
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
			shadBlurBuffer, aoBlurBuffer, reflBuffer, giBuffer);
		lightingDS = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDS, "descriptorSet.pbrLighting.base");
	}
	
	auto manager = Manager::Instance::get();
	if (options.useShadBuffer)
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
	if (options.useGiBuffer)
	{
		SET_CPU_ZONE_SCOPED("Pre GI Render");

		auto event = &manager->getEvent("PreGiRender");
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

	graphicsSystem->startRecording(CommandBufferType::Frame);
	if (options.useShadBuffer)
	{
		SET_CPU_ZONE_SCOPED("Shadows Render Pass");
		SET_GPU_DEBUG_LABEL("Shadows Pass");

		if (hasFbShad)
		{
			auto event = &manager->getEvent("ShadowRender");
			if (event->hasSubscribers())
			{
				RenderPass renderPass(shadFramebuffers[baseProcIndex], float4::zero);
				event->run();
			}
			hasFbShad = false;
		}

		auto shadBaseView = graphicsSystem->get(shadBaseBuffer);
		if (hasAnyShad)
		{
			if (quality > GraphicsQuality::Low)
			{
				auto depthBuffer = DeferredRenderSystem::Instance::get()->getDepthImageView();
				GpuProcessSystem::Instance::get()->bilateralBlurD(shadBaseView->getView(0, 1),
					depthBuffer, shadFramebuffers[blurProcIndex], shadFramebuffers[tempProcIndex], 
					blurSharpness, shadBlurPipeline, shadBlurDS, 5, true);
			}
			hasAnyShad = false;
		}
		else
		{
			auto shadBlurView = graphicsSystem->get(shadBlurBuffer);
			shadBlurView->clear(float4::one);
			shadBaseView->clear(float4::one);
		}
	}
	if (options.useAoBuffer)
	{
		SET_CPU_ZONE_SCOPED("AO Render Pass");
		SET_GPU_DEBUG_LABEL("AO Pass");

		auto event = &manager->getEvent("AoRender");
		if (event->hasSubscribers())
		{
			RenderPass renderPass(aoFramebuffers[baseProcIndex], float4::zero);
			event->run();
		}

		auto aoBaseView = graphicsSystem->get(aoBaseBuffer);
		if (hasAnyAO)
		{
			auto depthBuffer = DeferredRenderSystem::Instance::get()->getDepthImageView();
			GpuProcessSystem::Instance::get()->bilateralBlurD(aoBaseView->getView(0, 1), 
				depthBuffer, aoFramebuffers[blurProcIndex], aoFramebuffers[tempProcIndex], 
				blurSharpness, aoBlurPipeline, aoBlurDS);
			hasAnyAO = false;
		}
		else
		{
			auto aoBlurView = graphicsSystem->get(aoBlurBuffer);
			aoBlurView->clear(float4::one);
			aoBaseView->clear(float4::one);
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
			auto imageView = graphicsSystem->get(giBuffer);
			imageView->clear(float4(cc.ambientLight, 1.0f));
		}
	}
	if (options.useReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Reflections Render Pass");
		SET_GPU_DEBUG_LABEL("Reflection Pass");

		auto event = &manager->getEvent("ReflRender");
		if (event->hasSubscribers())
		{
			RenderPass renderPass(reflFramebuffer, float4::zero);
			event->run();
		}

		if (hasAnyRefl)
		{
			if (options.useReflBlur)
			{
				auto gpuProcessSystem = GpuProcessSystem::Instance::get();
				gpuProcessSystem->prepareGgxBlur(reflBuffer, reflFramebuffers);
				gpuProcessSystem->ggxBlur(reflBuffer, reflFramebuffers, reflBlurPipeline, reflBlurDSes);
			}
			hasAnyRefl = false;
		}
		else
		{
			auto imageView = graphicsSystem->get(reflBuffer);
			imageView->clear(float4::zero);
		}
	}
	graphicsSystem->stopRecording();

	if (options.useShadBuffer)
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
	if (options.useReflBuffer)
	{
		SET_CPU_ZONE_SCOPED("Post Reflections Render");

		auto event = &manager->getEvent("PostReflRender");
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
	auto pbrLightingView = Manager::Instance::get()->tryGet<PbrLightingComponent>(graphicsSystem->camera);
	if (!pbrLightingView)
		return;

	if (pbrLightingView->mode == PbrCubemapMode::Static)
	{
		if (!pbrLightingView->skybox || !pbrLightingView->shDiffuse || !pbrLightingView->specular)
			return;

		auto cubemapView = graphicsSystem->get(pbrLightingView->skybox);
		auto shDiffuseView = graphicsSystem->get(pbrLightingView->shDiffuse);
		auto specularView = graphicsSystem->get(pbrLightingView->specular);
		if (!cubemapView->isReady() || !shDiffuseView->isReady() || !specularView->isReady())
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
	if (!pbrLightingView->shDiffuse)
	{
		pbrLightingView->shDiffuse = Ref<Buffer>(graphicsSystem->createBuffer(
			Buffer::Usage::Uniform | Buffer::Usage::TransferDst, Buffer::CpuAccess::None, 
			3 * 4 * sizeof(f16x4), Buffer::Location::PreferGPU, Buffer::Strategy::Size));
		SET_RESOURCE_DEBUG_NAME(pbrLightingView->shDiffuse, 
			"buffer.uniform.shDiffuse" + to_string(*pbrLightingView->shDiffuse));
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
	pc.emissiveCoeff = cc.emissiveCoeff;
	pc.reflectanceCoeff = reflectanceCoeff;
	pc.ggxLodOffset = cc.ggxLodOffset;

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
	graphicsSystem->destroy(shadBlurDS);
	graphicsSystem->destroy(aoBlurDS);
	graphicsSystem->destroy(lightingDS);
	graphicsSystem->destroy(reflBlurDSes);

	if (shadBaseBuffer)
	{
		graphicsSystem->destroy(shadBaseBuffer); graphicsSystem->destroy(shadBlurBuffer);
		createProcBuffers(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadBufferFormat, "shad");
	}
	if (aoBaseBuffer)
	{
		graphicsSystem->destroy(aoBaseBuffer); graphicsSystem->destroy(aoBlurBuffer);
		createProcBuffers(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoBufferFormat, "ao");
	}
	if (reflBuffer)
	{
		graphicsSystem->destroy(reflBuffer);
		reflBuffer = createReflBuffer(graphicsSystem, options.useReflBlur);
	}
	if (giBuffer)
	{
		graphicsSystem->destroy(giBuffer);
		giBuffer = createGiBuffer(graphicsSystem);
	}

	if (shadFramebuffers[0])
		updateProcFramebuffer(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadFramebuffers);
	if (aoFramebuffers[0])
		updateProcFramebuffer(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoFramebuffers);

	if (!options.useReflBlur)
	{
		auto reflBufferView = graphicsSystem->get(reflBuffer)->getView();
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		auto colorAttachment = Framebuffer::OutputAttachment(reflBufferView, procFbFlags);
		auto framebufferView = graphicsSystem->get(reflFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}
	if (giFramebuffer)
	{
		auto giBufferView = graphicsSystem->get(giBuffer)->getView();
		auto framebufferSize = graphicsSystem->getScaledFrameSize();
		auto colorAttachment = Framebuffer::OutputAttachment(giBufferView, procFbFlags);
		auto framebufferView = graphicsSystem->get(giFramebuffer);
		framebufferView->update(framebufferSize, &colorAttachment, 1);
	}

	if (aoBaseBuffer || aoFramebuffers[0])
		Manager::Instance::get()->runEvent("AoRecreate");
	if (shadBaseBuffer || shadFramebuffers[0])
		Manager::Instance::get()->runEvent("ShadowRecreate");
	if (reflBuffer || reflFramebuffer || !reflFramebuffers.empty())
		Manager::Instance::get()->runEvent("ReflRecreate");
	if (giBuffer || giFramebuffer)
		Manager::Instance::get()->runEvent("GiRecreate");
}

void PbrLightingSystem::qualityChange()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (auto& component : components)
		graphicsSystem->destroy(component.descriptorSet);
	setQuality(graphicsSystem->quality);
}

//**********************************************************************************************************************
void PbrLightingSystem::resetComponent(View<Component> component, bool full)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto componentView = View<PbrLightingComponent>(component);
	graphicsSystem->destroy(componentView->skybox);
	graphicsSystem->destroy(componentView->shDiffuse);
	graphicsSystem->destroy(componentView->specular);
	graphicsSystem->destroy(componentView->descriptorSet);
}
void PbrLightingSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<PbrLightingComponent>(destination);
	const auto sourceView = View<PbrLightingComponent>(source);
	destinationView->skybox = sourceView->skybox;
	destinationView->shDiffuse = sourceView->shDiffuse;
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

	for (auto& pbrLighting : components)
		graphicsSystem->destroy(pbrLighting.descriptorSet);

	if (this->options.useShadBuffer != options.useShadBuffer)
	{
		if (options.useShadBuffer)
		{
			createProcBuffers(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadBufferFormat, "shad");
			createProcFramebuffers(graphicsSystem, shadBaseBuffer, shadBlurBuffer, shadFramebuffers, "shad");
		}
		else
		{
			graphicsSystem->destroy(shadBlurDS);
			graphicsSystem->destroy(shadFramebuffers, procBufferCount);
			graphicsSystem->destroy(shadBaseBuffer);
			graphicsSystem->destroy(aoBlurBuffer);
		}
	}
	if (this->options.useAoBuffer != options.useAoBuffer)
	{
		if (options.useAoBuffer)
		{
			createProcBuffers(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoBufferFormat, "ao");
			createProcFramebuffers(graphicsSystem, aoBaseBuffer, aoBlurBuffer, aoFramebuffers, "ao");
		}
		else
		{
			graphicsSystem->destroy(aoBlurDS);
			graphicsSystem->destroy(aoFramebuffers, procBufferCount);
			graphicsSystem->destroy(aoBaseBuffer);
			graphicsSystem->destroy(aoBlurBuffer);
		}
	}
	if (this->options.useReflBuffer != options.useReflBuffer || this->options.useReflBlur != options.useReflBlur)
	{
		if (options.useReflBuffer)
		{
			reflBuffer = createReflBuffer(graphicsSystem, options.useReflBlur);
			if (!options.useReflBlur)
				reflFramebuffer = createReflFramebuffer(graphicsSystem, reflBuffer);
		}
		else
		{
			graphicsSystem->destroy(reflBlurDSes); reflBlurDSes = {};
			graphicsSystem->destroy(reflFramebuffers); reflFramebuffers = {};
			graphicsSystem->destroy(reflFramebuffer);
			graphicsSystem->destroy(reflBuffer);
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
		if (dfgLutView->getSize().getX() != getDfgLutSize(quality))
		{
			graphicsSystem->destroy(lightingDS);
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

const ID<Framebuffer>* PbrLightingSystem::getShadFramebuffers()
{
	if (!shadFramebuffers[0] && options.useShadBuffer)
	{
		createProcFramebuffers(GraphicsSystem::Instance::get(), 
			getShadBaseBuffer(), getShadBlurBuffer(), shadFramebuffers, "shad");
	}
	return shadFramebuffers;
}
const ID<Framebuffer>* PbrLightingSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0] && options.useAoBuffer)
	{
		createProcFramebuffers(GraphicsSystem::Instance::get(), 
			getAoBaseBuffer(), getAoBlurBuffer(), aoFramebuffers, "ao");
	}
	return aoFramebuffers;
}
const vector<ID<Framebuffer>>& PbrLightingSystem::getReflFramebuffers()
{
	if (reflFramebuffers.empty() && options.useReflBuffer && options.useReflBlur)
		GpuProcessSystem::Instance::get()->prepareGgxBlur(getReflBuffer(), reflFramebuffers);
	return reflFramebuffers;
}
ID<Framebuffer> PbrLightingSystem::getReflFramebuffer()
{
	if (!reflFramebuffer && options.useReflBuffer && !options.useReflBlur)
		reflFramebuffer = createReflFramebuffer(GraphicsSystem::Instance::get(), getReflBuffer());
	return reflFramebuffer;
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

ID<Image> PbrLightingSystem::getShadBaseBuffer()
{
	if (!shadBaseBuffer && options.useShadBuffer)
		createProcBuffers(GraphicsSystem::Instance::get(), shadBaseBuffer, shadBlurBuffer, shadBufferFormat, "shad");
	return shadBaseBuffer;
}
ID<Image> PbrLightingSystem::getShadBlurBuffer()
{
	if (!shadBaseBuffer && options.useShadBuffer)
		createProcBuffers(GraphicsSystem::Instance::get(), shadBaseBuffer, shadBlurBuffer, shadBufferFormat, "shad");
	return shadBlurBuffer;
}
ID<Image> PbrLightingSystem::getAoBaseBuffer()
{
	if (!aoBaseBuffer && options.useAoBuffer)
		createProcBuffers(GraphicsSystem::Instance::get(), aoBaseBuffer, aoBlurBuffer, aoBufferFormat, "ao");
	return aoBaseBuffer;
}
ID<Image> PbrLightingSystem::getAoBlurBuffer()
{
	if (!aoBaseBuffer && options.useAoBuffer)
		createProcBuffers(GraphicsSystem::Instance::get(), aoBaseBuffer, aoBlurBuffer, aoBufferFormat, "ao");
	return aoBlurBuffer;
}
ID<Image> PbrLightingSystem::getReflBuffer()
{
	if (!reflBuffer && options.useReflBuffer)
		reflBuffer = createReflBuffer(GraphicsSystem::Instance::get(), options.useReflBlur);
	return reflBuffer;
}
ID<Image> PbrLightingSystem::getGiBuffer()
{
	if (!giBuffer && options.useGiBuffer)
		giBuffer = createGiBuffer(GraphicsSystem::Instance::get());
	return giBuffer;
}

ID<ImageView> PbrLightingSystem::getShadBaseView()
{
	if (!options.useShadBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getShadBaseBuffer())->getView(0, 1);
}
ID<ImageView> PbrLightingSystem::getShadTempView()
{
	if (!options.useShadBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getShadBaseBuffer())->getView(0, 0);
}
ID<ImageView> PbrLightingSystem::getShadBlurView()
{
	if (!options.useShadBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getShadBlurBuffer())->getView();
}
ID<ImageView> PbrLightingSystem::getAoBaseView()
{
	if (!options.useAoBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getAoBaseBuffer())->getView(0, 1);
}
ID<ImageView> PbrLightingSystem::getAoTempView()
{
	if (!options.useAoBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getAoBaseBuffer())->getView(0, 0);
}
ID<ImageView> PbrLightingSystem::getAoBlurView()
{
	if (!options.useAoBuffer)
		return {};
	return GraphicsSystem::Instance::get()->get(getAoBlurBuffer())->getView();
}
ID<ImageView> PbrLightingSystem::getReflBaseView()
{
	if (!options.useReflBuffer)
		return {};
	auto reflBufferView = GraphicsSystem::Instance::get()->get(getReflBuffer());
	return options.useReflBlur ? reflBufferView->getView(0, 0) : reflBufferView->getView();
}

//**********************************************************************************************************************
static void calcShDiffuse(f32x4* shBufferData, const float4* const* faces, 
	uint32 cubemapSize, uint32 taskIndex, uint32 itemOffset, uint32 itemCount)
{
	auto sh = shBufferData + taskIndex * sh3Count;
	auto invDim = 1.0f / cubemapSize, saArea = invDim * invDim * 4.0f;
	float shb[sh3Count];

	for (uint32 face = 0; face < Image::cubemapFaceCount; face++)
	{
		auto pixels = faces[face];
		for (uint32 i = itemOffset; i < itemCount; i++)
		{
			auto y = i / cubemapSize, x = i - y * cubemapSize;
			auto st = coordsToST(uint2(x, y), invDim);
			auto dir = stToDir(st, face);
			auto color = f32x4(pixels[y * cubemapSize + x]);
			color *= calcSolidAngleFastA(st, saArea);
			computeSh3Basis(dir, shb);

			for (uint32 j = 0; j < sh3Count; j++)
				sh[j] += color * shb[j];
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
	auto logOmegaP = log4(float(M_PI * 4.0) / (cubemapSize * cubemapSize * 6.0f));
	float weight = 0.0f; uint32 count = 0;

	for (uint32 i = 0; i < iblSampleCount; i++)
	{
		auto u = hammersley(i, invIblSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto noh = h.getZ(), noh2 = noh * noh; auto nol = fma(noh2, 2.0f, -1.0f);

		if (nol > 0.0f)
		{
			constexpr auto k = 1.0f; // log4(4.0f);
			auto pdf = ggx(noh, linearRoughness) / 4.0f;
			auto omegaS = 1.0f / (iblSampleCount * pdf);
			auto level = log4(omegaS) - logOmegaP + k;
			auto mip = clamp(level, 0.0f, (float)(skyboxMipCount - 1));
			auto l = float3(noh * h.getX() * 2.0f, noh * h.getY() * 2.0f, nol);
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
	vector<float>& iblWeightBuffer, vector<uint32>& iblCountBuffer, Buffer::Usage usage)
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
	auto stagingBuffer = graphicsSystem->createStagingBuffer(Buffer::CpuAccess::RandomReadWrite, specularCacheSize);
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

	auto specularCache = graphicsSystem->createBuffer(usage, Buffer::CpuAccess::None, 
		gpuCacheSize, Buffer::Location::PreferGPU, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(specularCache, "buffer.storage.specularCache" + to_string(*specularCache));

	auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::TransferOnly);
	{
		SET_GPU_DEBUG_LABEL("Generate Specular Cache");
		specularCacheSize = gpuCacheSize = 0;
		Buffer::CopyRegion bufferCopyRegion;

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
	}

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
			Image::Type::Texture2DArray, specularFormat, 0, Image::cubemapFaceCount, i + 1, 1);
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
	auto skyboxView = graphicsSystem->get(skybox)->getView();
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
		pc.sampleCount = pc.sampleOffset + iblCountBuffer[i];
		pc.weight = iblWeightBuffer[i];
		pipelineView->pushConstants(&pc);

		// TODO: Maybe there are faster approaches for IBL specular evaluation. Check Unreal's approach.
		pipelineView->dispatch(uint3(cubemapSize, cubemapSize, faceCount));
		pc.sampleOffset += pc.sampleCount; cubemapSize /= 2;
	}
}

//**********************************************************************************************************************
void PbrLightingSystem::processShDiffuse(f32x4* shBuffer, bool dering) noexcept
{
	GARDEN_ASSERT(shBuffer);
	applyKiSh3(shBuffer);
	if (dering) deringSh3(shBuffer);
	preprocessSh3(shBuffer);
}

//**********************************************************************************************************************
void PbrLightingSystem::generateShDiffuse(const float4* const* skyboxFaces, 
	uint32 skyboxSize, vector<f32x4>& shCache, bool dering)
{
	GARDEN_ASSERT(skyboxFaces);
	GARDEN_ASSERT(skyboxSize > 0);

	auto threadSystem = ThreadSystem::Instance::tryGet();
	uint32 bufferCount;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		bufferCount = threadPool.getThreadCount();
		shCache.resize(bufferCount * sh3Count);
		auto shCacheData = shCache.data();

		threadPool.addItems([shCacheData, skyboxFaces, skyboxSize](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("IBL SH Generate");
			calcShDiffuse(shCacheData, skyboxFaces, skyboxSize, 
				task.getTaskIndex(), task.getItemOffset(), task.getItemCount());
		},
		skyboxSize * skyboxSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("IBL SH Generate");
		bufferCount = 1; shCache.resize(sh3Count);
		calcShDiffuse(shCache.data(), skyboxFaces, skyboxSize, 0, 0, skyboxSize * skyboxSize);
	}

	auto shCacheData = shCache.data();
	if (bufferCount > 1)
	{
		for (uint32 i = 1; i < bufferCount; i++)
		{
			auto data = shCacheData + i * sh3Count;
			for (uint32 j = 0; j < sh3Count; j++)
				shCacheData[j] += data[j];
		}
		shCache.resize(sh3Count);
	}

	processShDiffuse(shCacheData, dering);
}

//**********************************************************************************************************************
void PbrLightingSystem::loadCubemap(const fs::path& path, Image::Format format, Ref<Image>& cubemap,
	Ref<Buffer>& shDiffuse, Ref<Image>& specular, Memory::Strategy strategy, f32x4x4* shCoeffs, vector<f32x4>* shCache)
{
	GARDEN_ASSERT(!path.empty());
	SET_CPU_ZONE_SCOPED("PBR Cubemap Load");
	
	vector<uint8> left, right, bottom, top, back, front; uint2 size;
	ResourceSystem::Instance::get()->loadCubemapData(path, format, left, right, bottom, top, back, front, size, true);
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

		vector<f32x4> localShCache; if (!shCache) shCache = &localShCache;
		generateShDiffuse((const float4* const*)mips[0].data(), cubemapSize, *shCache);
		auto shCacheData = shCache->data();

		if (shCoeffs)
		{
			shCoeffs[0] = f32x4x4(shCacheData[0], shCacheData[1], shCacheData[2], f32x4::zero);
			shCoeffs[1] = f32x4x4(shCacheData[3], shCacheData[4], shCacheData[5], f32x4::zero);
			shCoeffs[2] = f32x4x4(shCacheData[6], shCacheData[7], shCacheData[8], f32x4::zero);
		}

		for (uint8 i = 0; i < sh3Count; i++)
			shCacheData[i] = min(shCacheData[i], f32x4(65504.0f));

		f16x4 shCoeffs16[3 * 4] =
		{
			(f16x4)shCacheData[0], (f16x4)shCacheData[1], (f16x4)shCacheData[2], f16x4(f32x4::zero),
			(f16x4)shCacheData[3], (f16x4)shCacheData[4], (f16x4)shCacheData[5], f16x4(f32x4::zero),
			(f16x4)shCacheData[6], (f16x4)shCacheData[7], (f16x4)shCacheData[8], f16x4(f32x4::zero)
		};

		shDiffuse = Ref<Buffer>(graphicsSystem->createBuffer(Buffer::Usage::Uniform | 
			Buffer::Usage::TransferDst, Buffer::CpuAccess::None, shCoeffs16, 
			3 * 4 * sizeof(f16x4), Buffer::Location::PreferGPU, strategy));
		SET_RESOURCE_DEBUG_NAME(shDiffuse, "buffer.uniform.shBuffer." + path.generic_string());

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
	
		graphicsSystem->destroy(iblDescriptorSets);
		graphicsSystem->destroy(specularViews);
		graphicsSystem->destroy(specularCache);
	}
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
Ref<DescriptorSet> PbrLightingSystem::createDescriptorSet(ID<Entity> entity, 
	ID<Pipeline> pipeline, PipelineType type, uint8 index)
{
	GARDEN_ASSERT(entity);

	auto pbrLightingView = Manager::Instance::get()->tryGet<PbrLightingComponent>(entity);
	if (!lightingDS || !pbrLightingView || !pbrLightingView->shDiffuse || !pbrLightingView->specular)
		return {};

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto shDiffuseView = graphicsSystem->get(pbrLightingView->shDiffuse);
	auto specularView = graphicsSystem->get(pbrLightingView->specular);
	if (!shDiffuseView->isReady() || !specularView->isReady())
		return {};

	DescriptorSet::Uniforms iblUniforms =
	{ 
		{ "sh", DescriptorSet::Uniform(ID<Buffer>(pbrLightingView->shDiffuse)) },
		{ "specular", DescriptorSet::Uniform(specularView->getView()) }
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
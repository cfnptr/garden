// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/render/deferred.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
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
		float4 l = float4(0.0f);
		float4 nolMip = float4(0.0f);

		constexpr SpecularItem(const float3& l, float nol, float mip) noexcept :
			l(float4(l, 0.0f)), nolMip(float4(nol, mip, 0.0f, 0.0f)) { }
		constexpr SpecularItem() = default;
	};
}

#if 0 // Used to precompute Ki coefs.
//**********************************************************************************************************************
static uint32 factorial(uint32 x)
{
	return x == 0 ? 1 : x * factorial(x - 1);
}
static double factorial(int32 n, int32 d)
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
static double computeKml(int32 m, int32 l)
{
	auto k = (2 * l + 1) * factorial(l - m, l + m);
	return sqrt(k) * (M_2_SQRTPI * 0.25);
}
static double computeTruncatedCosSh(uint32 l)
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
static void computeKi()
{
	double ki[shCoefCount];

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

	for (uint32 i = 0; i < shCoefCount; i++)
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
	auto v = float3(sqrt(1.0f - nov * nov), 0.0f, nov);
	auto invSampleCount = 1.0f / SPECULAR_SAMPLE_COUNT;
	auto linearRoughness = coord * coord;

	auto r = float2(0.0f);

	for (uint32 i = 0; i < SPECULAR_SAMPLE_COUNT; i++)
	{
		auto u = hammersley(i, invSampleCount);
		auto h = importanceSamplingNdfDggx(u, linearRoughness);
		auto voh = dot(v, h);
		auto l = 2.0f * voh * h - v;
		voh = clamp(voh, 0.0f, 1.0f);
		auto nol = clamp(l.z, 0.0f, 1.0f);
		auto noh = clamp(h.z, 0.0f, 1.0f);

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
	auto image = graphicsSystem->createImage(Image::Format::UnormR8, Image::Bind::ColorAttachment |
		Image::Bind::Sampled | Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.shadow.buffer");

	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount; i++)
	{
		shadowImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
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

static void createShadowFramebuffers(ID<Framebuffer>* shadowFramebuffers, const ID<ImageView>* shadowImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(shadowImageViews[i], true, false, true) };
		shadowFramebuffers[i] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(shadowFramebuffers[i], "framebuffer.lighting.shadow" + to_string(i));
	}
}
static void destroyShadowFramebuffers(ID<Framebuffer>* shadowFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingRenderSystem::shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowFramebuffers[i]);
		shadowFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createAoBuffer(ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	Image::Mips mips(1); mips[0].assign(PbrLightingRenderSystem::aoBufferCount, nullptr);
	auto image = graphicsSystem->createImage(Image::Format::UnormR8, Image::Bind::ColorAttachment |
		Image::Bind::Sampled | Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.ao.buffer");

	for (uint32 i = 0; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		aoImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(aoImageViews[i], "imageView.lighting.ao" + to_string(i));
	}

	return image;
}
static void destroyAoBuffer(ID<Image> aoBuffer, ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	for (uint32 i = 0; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoImageViews[i]);
		aoImageViews[i] = {};
	}
	graphicsSystem->destroy(aoBuffer);
}

static void createAoFramebuffers(ID<Framebuffer>* aoFramebuffers, const ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	for (uint32 i = 0; i < PbrLightingRenderSystem::aoBufferCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(aoImageViews[i], true, false, true) };
		aoFramebuffers[i] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(aoFramebuffers[i], "framebuffer.lighting.ao" + to_string(i));
	}
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
static map<string, DescriptorSet::Uniform> getLightingUniforms(ID<Image> dfgLUT,
	ID<ImageView> shadowImageViews[PbrLightingRenderSystem::shadowBufferCount],
	ID<ImageView> aoImageViews[PbrLightingRenderSystem::aoBufferCount])
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::Instance::get()->getGFramebuffer());
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
	auto depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0] ? // TODO: [1]
			shadowImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "aoBuffer", DescriptorSet::Uniform(aoImageViews[1] ?
			aoImageViews[1] : graphicsSystem->getWhiteTexture()) },
		{ "dfgLUT", DescriptorSet::Uniform(graphicsSystem->get(dfgLUT)->getDefaultView()) }
	};

	for (uint8 i = 0; i < DeferredRenderSystem::gBufferCount; i++)
		uniforms.emplace("g" + to_string(i), DescriptorSet::Uniform(colorAttachments[i].imageView));

	return uniforms;
}

static map<string, DescriptorSet::Uniform> getShadowDenoiseUniforms(
	ID<ImageView> shadowImageViews[PbrLightingRenderSystem::shadowBufferCount])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0]) } };
	return uniforms;
}
static map<string, DescriptorSet::Uniform> getAoDenoiseUniforms(
	ID<ImageView> aoImageViews[PbrLightingRenderSystem::aoBufferCount])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "aoBuffer", DescriptorSet::Uniform(aoImageViews[0]) } };
	return uniforms;
}

static ID<GraphicsPipeline> createLightingPipeline(bool useShadowBuffer, bool useAoBuffer)
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	map<string, Pipeline::SpecConstValue> specConstValues =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConstValue(useShadowBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConstValue(useAoBuffer) }
	};

	return ResourceSystem::Instance::get()->loadGraphicsPipeline("pbr-lighting", 
		deferredSystem->getHdrFramebuffer(), deferredSystem->useAsyncRecording(), true, 0, 0, specConstValues);
}
static ID<ComputePipeline> createIblSpecularPipeline()
{
	return ResourceSystem::Instance::get()->loadComputePipeline("ibl-specular", false, false);
}
static ID<GraphicsPipeline> createAoDenoisePipeline(
	const ID<Framebuffer> aoFramebuffers[PbrLightingRenderSystem::aoBufferCount])
{
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("denoise/ao", aoFramebuffers[1]);
}

//**********************************************************************************************************************
static ID<Image> createDfgLUT()
{
	auto pixels = malloc<float2>(iblDfgSize * iblDfgSize);

	// TODO: check if release build DFG image looks the same as debug.
	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([&](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("DFG LUT Create");

			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto y = i / iblDfgSize, x = i - y * iblDfgSize;
				pixels[i] = dfvMultiscatter(x, (iblDfgSize - 1) - y);
			}
		},
		iblDfgSize * iblDfgSize);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("DFG LUT Create");

		auto pixelData = (float2*)pixels; uint32 index = 0;
		for (int32 y = iblDfgSize - 1; y >= 0; y--)
		{
			for (uint32 x = 0; x < iblDfgSize; x++)
				pixelData[index++] = dfvMultiscatter(x, y);
		}
	}
	
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16, Image::Bind::TransferDst |
		Image::Bind::Sampled, { { pixels } }, uint2(iblDfgSize), Image::Strategy::Size, Image::Format::SfloatR32G32);
	SET_RESOURCE_DEBUG_NAME(image, "image.lighting.dfgLUT");
	free(pixels);
	return image;
}

//**********************************************************************************************************************
bool PbrLightingRenderComponent::destroy()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (cubemap.isLastRef())
		graphicsSystem->destroy(ID<Image>(cubemap));
	if (sh.isLastRef())
		graphicsSystem->destroy(ID<Buffer>(sh));
	if (specular.isLastRef())
		graphicsSystem->destroy(ID<Image>(specular));
	if (descriptorSet.isLastRef())
		graphicsSystem->destroy(ID<DescriptorSet>(descriptorSet));
	cubemap = {}; sh = {}; specular = {}; descriptorSet = {};
	return true;
}

PbrLightingRenderSystem::PbrLightingRenderSystem(bool useShadowBuffer, bool useAoBuffer, bool setSingleton) :
	Singleton(setSingleton), hasShadowBuffer(useShadowBuffer), hasAoBuffer(useAoBuffer)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", PbrLightingRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PbrLightingRenderSystem::deinit);
}
PbrLightingRenderSystem::~PbrLightingRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PbrLightingRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PbrLightingRenderSystem::deinit);
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
			createShadowFramebuffers(shadowFramebuffers, shadowImageViews);
	}
	if (hasAoBuffer)
	{
		if (!aoBuffer)
			aoBuffer = createAoBuffer(aoImageViews);
		if (!aoFramebuffers[0])
			createAoFramebuffers(aoFramebuffers, aoImageViews);
		if (!aoDenoisePipeline)
			aoDenoisePipeline = createAoDenoisePipeline(aoFramebuffers);
	}	

	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(hasShadowBuffer, hasAoBuffer);
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();
}
void PbrLightingRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(iblSpecularPipeline);
		graphicsSystem->destroy(lightingPipeline);
		graphicsSystem->destroy(aoDenoisePipeline);
		destroyAoFramebuffers(aoFramebuffers);
		destroyAoBuffer(aoBuffer, aoImageViews);
		destroyShadowFramebuffers(aoFramebuffers);
		destroyShadowBuffer(shadowBuffer, shadowImageViews);

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
	if (pipelineView->isReady() && !lightingDescriptorSet)
	{
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews);
		lightingDescriptorSet = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(lightingDescriptorSet, "descriptorSet.lighting.base");
	}

	const auto& systems = Manager::Instance::get()->getSystems();
	shadowSystems.clear();
	aoSystems.clear();

	for (const auto& pair : systems)
	{
		auto shadowSystem = dynamic_cast<IShadowRenderSystem*>(pair.second);
		if (shadowSystem)
			shadowSystems.push_back(shadowSystem);
		auto aoSystem = dynamic_cast<IAoRenderSystem*>(pair.second);
		if (aoSystem)
			aoSystems.push_back(aoSystem);
	}

	auto hasAnyShadow = false, hasAnyAO = false;
	if (hasShadowBuffer && !shadowSystems.empty())
	{
		SET_GPU_DEBUG_LABEL("Pre Shadow Pass", Color::transparent);
		for (auto shadowSystem : shadowSystems)
			shadowSystem->preShadowRender();
	}
	if (hasAoBuffer && !aoSystems.empty())
	{
		SET_GPU_DEBUG_LABEL("Pre AO Pass", Color::transparent);
		for (auto aoSystem : aoSystems)
			aoSystem->preAoRender();
	}

	if (hasShadowBuffer && !shadowSystems.empty())
	{
		if (!shadowBuffer)
			shadowBuffer = createShadowBuffer(shadowImageViews);
		if (!shadowFramebuffers[0])
			createShadowFramebuffers(shadowFramebuffers, shadowImageViews);

		SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(shadowFramebuffers[0]);
		framebufferView->beginRenderPass(float4(1.0f));
		for (auto shadowSystem : shadowSystems)
			hasAnyShadow |= shadowSystem->shadowRender();
		framebufferView->endRenderPass();
	}
	if (hasAoBuffer && !aoSystems.empty())
	{
		if (!aoBuffer)
			aoBuffer = createAoBuffer(aoImageViews);
		if (!aoFramebuffers[0])
			createAoFramebuffers(aoFramebuffers, aoImageViews);
		if (!aoDenoisePipeline)
			aoDenoisePipeline = createAoDenoisePipeline(aoFramebuffers);

		SET_GPU_DEBUG_LABEL("AO Pass", Color::transparent);
		auto framebufferView = graphicsSystem->get(aoFramebuffers[0]);
		framebufferView->beginRenderPass(float4(1.0f));
		for (auto aoSystem : aoSystems)
			hasAnyAO |= aoSystem->aoRender();
		framebufferView->endRenderPass();
	}

	if (shadowBuffer && !hasAnyShadow)
	{
		auto imageView = graphicsSystem->get(shadowBuffer);
		imageView->clear(float4(1.0f));
	}

	if (hasAnyAO)
	{
		auto aoPipelineView = graphicsSystem->get(aoDenoisePipeline);
		if (aoPipelineView->isReady())
		{
			if (!aoDenoiseDescriptorSet)
			{
				auto uniforms = getAoDenoiseUniforms(aoImageViews);
				aoDenoiseDescriptorSet = graphicsSystem->createDescriptorSet(aoDenoisePipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(aoDenoiseDescriptorSet, "descriptorSet.lighting.ao-denoise");
			}

			SET_GPU_DEBUG_LABEL("AO Denoise Pass", Color::transparent);
			auto framebufferView = graphicsSystem->get(aoFramebuffers[1]);
			framebufferView->beginRenderPass(float4(1.0f));
			aoPipelineView->bind();
			aoPipelineView->setViewportScissor();
			aoPipelineView->bindDescriptorSet(aoDenoiseDescriptorSet);
			aoPipelineView->drawFullscreen();
			framebufferView->endRenderPass();
		}
	}
	else if (aoBuffer)
	{
		auto imageView = graphicsSystem->get(aoBuffer);
		imageView->clear(float4(1.0f));
	}
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
	if (!pipelineView->isReady() || !dfgLutView->isReady() || !lightingDescriptorSet)
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

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	constexpr auto uvToNDC = float4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	DescriptorSet::Range descriptorSetRange[2];
	descriptorSetRange[0] = DescriptorSet::Range(lightingDescriptorSet);
	descriptorSetRange[1] = DescriptorSet::Range(ID<DescriptorSet>(pbrLightingView->descriptorSet));

	auto pushConstants = pipelineView->getPushConstants<LightingPC>();
	pushConstants->uvToWorld = cameraConstants.viewProjInv * uvToNDC;
	pushConstants->shadowColor = float4((float3)shadowColor * shadowColor.w, 0.0f);
	pushConstants->emissiveMult = emissiveMult;

	SET_GPU_DEBUG_LABEL("PBR Lighting", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSets(descriptorSetRange, 2);
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();

	if (aoBuffer)
	{
		destroyAoBuffer(aoBuffer, aoImageViews);
		aoBuffer = createAoBuffer(aoImageViews);
	}
	if (aoFramebuffers[0])
	{
		for (uint32 i = 0; i < aoBufferCount; i++)
		{
			auto framebufferView = graphicsSystem->get(aoFramebuffers[i]);
			Framebuffer::OutputAttachment colorAttachment(aoImageViews[i], true, false, true);
			framebufferView->update(framebufferSize, &colorAttachment, 1);
		}
	}

	if (shadowBuffer)
	{
		destroyShadowBuffer(shadowBuffer, shadowImageViews);
		shadowBuffer = createShadowBuffer(shadowImageViews);
	}
	if (shadowFramebuffers[0])
	{
		for (uint32 i = 0; i < shadowBufferCount; i++)
		{
			auto framebufferView = graphicsSystem->get(shadowFramebuffers[i]);
			Framebuffer::OutputAttachment colorAttachment(shadowImageViews[i], true, false, true);
			framebufferView->update(framebufferSize, &colorAttachment, 1);
		}
	}

	if (lightingDescriptorSet)
	{
		auto descriptorSetView = graphicsSystem->get(lightingDescriptorSet);
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews);
		descriptorSetView->recreate(std::move(uniforms));
	}
	if (aoDenoiseDescriptorSet)
	{
		auto descriptorSetView = graphicsSystem->get(aoDenoiseDescriptorSet);
		auto uniforms = getAoDenoiseUniforms(aoImageViews);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

//**********************************************************************************************************************
const string& PbrLightingRenderSystem::getComponentName() const
{
	static const string name = "PBR Lighting";
	return name;
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::setConsts(bool useShadowBuffer, bool useAoBuffer)
{
	if (this->hasShadowBuffer == useShadowBuffer && this->hasAoBuffer == useAoBuffer)
		return;

	this->hasShadowBuffer = useShadowBuffer; this->hasAoBuffer = useAoBuffer;
	if (!lightingPipeline)
		return;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->destroy(lightingDescriptorSet);
	lightingDescriptorSet = {};

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
			createShadowFramebuffers(shadowFramebuffers, shadowImageViews);
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
			aoBuffer = createAoBuffer(aoImageViews);
			createAoFramebuffers(aoFramebuffers, aoImageViews);
		}
		else
		{
			destroyAoFramebuffers(aoFramebuffers);
			destroyAoBuffer(aoBuffer, aoImageViews);
			aoBuffer = {};
		}
	}

	graphicsSystem->destroy(lightingPipeline);
	lightingPipeline = createLightingPipeline(useShadowBuffer, useAoBuffer);
}

//**********************************************************************************************************************
ID<GraphicsPipeline> PbrLightingRenderSystem::getLightingPipeline()
{
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(hasShadowBuffer, hasAoBuffer);
	return lightingPipeline;
}
ID<ComputePipeline> PbrLightingRenderSystem::getIblSpecularPipeline()
{
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();
	return iblSpecularPipeline;
}
ID<GraphicsPipeline> PbrLightingRenderSystem::getAoDenoisePipeline()
{
	if (!aoDenoisePipeline)
		aoDenoisePipeline = createAoDenoisePipeline(getAoFramebuffers());
	return aoDenoisePipeline;
}

const ID<Framebuffer>* PbrLightingRenderSystem::getShadowFramebuffers()
{
	if (!shadowFramebuffers[0])
		createShadowFramebuffers(shadowFramebuffers, getShadowImageViews());
	return shadowFramebuffers;
}
const ID<Framebuffer>* PbrLightingRenderSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0])
		createAoFramebuffers(aoFramebuffers, getAoImageViews());
	return aoFramebuffers;
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
	if (!shadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowBuffer;
}
ID<Image> PbrLightingRenderSystem::getAoBuffer()
{
	if (!aoBuffer)
		aoBuffer = createAoBuffer(aoImageViews);
	return aoBuffer;
}
const ID<ImageView>* PbrLightingRenderSystem::getShadowImageViews()
{
	if (!shadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowImageViews;
}
const ID<ImageView>* PbrLightingRenderSystem::getAoImageViews()
{
	if (!aoBuffer)
		aoBuffer = createAoBuffer(aoImageViews);
	return aoImageViews;
}

//**********************************************************************************************************************
static void calcIblSH(float4* shBufferData, const float4** faces, uint32 cubemapSize, 
	uint32 taskIndex, uint32 itemOffset, uint32 itemCount)
{
	auto sh = shBufferData + taskIndex * shCoefCount;
	auto invDim = 1.0f / cubemapSize;
	float shb[shCoefCount];

	for (uint8 face = 0; face < 6; face++)
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

			for (uint32 j = 0; j < shCoefCount; j++)
				sh[j] += color * shb[j];
		}
	}
}

//**********************************************************************************************************************
static ID<Buffer> generateIblSH(ThreadSystem* threadSystem,
	const vector<const void*>& _pixels, uint32 cubemapSize, Buffer::Strategy strategy)
{
	auto faces = (const float4**)_pixels.data();
	vector<float4> shBuffer;
	float4* shBufferData;
	uint32 bufferCount;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		bufferCount = threadPool.getThreadCount();
		shBuffer.resize(bufferCount * shCoefCount);
		shBufferData = shBuffer.data();

		threadPool.addItems([&](const ThreadPool::Task& task)
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
		shBuffer.resize(shCoefCount);
		shBufferData = shBuffer.data();
		calcIblSH(shBufferData, faces, cubemapSize, 0, 0, cubemapSize * cubemapSize);
	}

	if (bufferCount > 1)
	{
		for (uint32 i = 1; i < bufferCount; i++)
		{
			auto data = shBufferData + i * shCoefCount;
			for (uint32 j = 0; j < shCoefCount; j++)
				shBufferData[j] += data[j];
		}
	}

	for (uint32 i = 0; i < shCoefCount; i++)
		shBufferData[i] *= ki[i];
	
	deringingSH(shBufferData);
	shaderPreprocessSH(shBufferData);

	// TODO: check if final SH is the same as debug in release build.
	return GraphicsSystem::Instance::get()->createBuffer(Buffer::Bind::TransferDst | Buffer::Bind::Uniform,
		Buffer::Access::None, shBufferData, shCoefCount * sizeof(float4), Buffer::Usage::PreferGPU, strategy);
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
	auto roughness = mipToLinearRoughness(specularMipCount, mipIndex);
	auto logOmegaP = log4((4.0f * (float)M_PI) / (6.0f * cubemapSize * cubemapSize));
	float weight = 0.0f; uint32 count = 0;

	for (uint32 i = 0; i < sampleCount; i++)
	{
		auto u = hammersley(i, invSampleCount);
		auto h = importanceSamplingNdfDggx(u, roughness);
		auto noh = h.z, noh2 = h.z * h.z;
		auto nol = 2.0f * noh2 - 1.0f;
		auto l = float3(2.0f * noh * h.x, 2.0f * noh * h.y, nol);

		if (nol > 0.0f)
		{
			constexpr auto k = 1.0f; // log4(4.0f);
			auto pdf = ggx(noh, roughness) / 4.0f;
			auto omegaS = 1.0f / (sampleCount * pdf);
			auto level = log4(omegaS) - logOmegaP + k;
			auto mip = clamp(level, 0.0f, (float)(cubemapMipCount - 1));
			map[count++] = SpecularItem(l, nol, mip);
			weight += nol;
		}
	}

	auto invWeight = 1.0f / weight;
	for (uint32 i = 0; i < count; i++)
		map[i].nolMip.x *= invWeight;
		
	qsort(map, count, sizeof(SpecularItem), [](const void* a, const void* b)
	{
		auto aa = (const SpecularItem*)a; auto bb = (const SpecularItem*)b;
		if (aa->nolMip.x < bb->nolMip.x)
			return -1;
		if (aa->nolMip.x > bb->nolMip.x)
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
	auto cubemapSize = cubemapView->getSize().x;
	auto cubemapFormat = cubemapView->getFormat();
	auto cubemapMipCount = cubemapView->getMipCount();
	auto defaultCubemapView = cubemapView->getDefaultView();
	
	const uint8 maxMipCount = 5; // Optimal value based on filament research.
	auto specularMipCount = std::min(calcMipCount(cubemapSize), maxMipCount);
	Image::Mips mips(specularMipCount);

	for (uint8 i = 0; i < specularMipCount; i++)
		mips[i] = Image::Layers(6);

	auto specular = graphicsSystem->createImage(Image::Type::Cubemap,
		Image::Format::SfloatR16G16B16A16, Image::Bind::TransferDst | Image::Bind::Storage | 
		Image::Bind::Sampled, mips, uint3(cubemapSize, cubemapSize, 1), strategy);

	uint64 specularCacheSize = 0;
	for (uint8 i = 1; i < specularMipCount; i++)
		specularCacheSize += calcSampleCount(i);
	specularCacheSize *= sizeof(SpecularItem);

	auto cpuSpecularCache = graphicsSystem->createBuffer(
		Buffer::Bind::TransferSrc, Buffer::Access::RandomReadWrite,
		specularCacheSize, Buffer::Usage::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(cpuSpecularCache,
		"buffer.storage.lighting.cpuSpecularCache" + to_string(*cpuSpecularCache));
	auto cpuSpecularCacheView = graphicsSystem->get(cpuSpecularCache);

	vector<uint32> countBuffer(specularMipCount - 1);
	vector<ID<Buffer>> gpuSpecularCaches(countBuffer.size());
	auto countBufferData = countBuffer.data();
	auto specularMap = (SpecularItem*)cpuSpecularCacheView->getMap();

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks([&](const ThreadPool::Task& task)
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
	for (uint32 i = 0; i < (uint32)gpuSpecularCaches.size(); i++)
	{
		auto cacheSize = countBuffer[i] * sizeof(SpecularItem);
		auto gpuSpecularCache = graphicsSystem->createBuffer(Buffer::Bind::Storage |
			Buffer::Bind::TransferDst, Buffer::Access::None, cacheSize,
			Buffer::Usage::PreferGPU, Buffer::Strategy::Speed);
		SET_RESOURCE_DEBUG_NAME(gpuSpecularCache,
			"buffer.storage.lighting.gpuSpecularCache" + to_string(*gpuSpecularCache));
		gpuSpecularCaches[i] = gpuSpecularCache;

		Buffer::CopyRegion bufferCopyRegion;
		bufferCopyRegion.size = cacheSize;
		bufferCopyRegion.srcOffset = specularCacheSize;
		Buffer::copy(cpuSpecularCache, gpuSpecularCache, bufferCopyRegion);
		specularCacheSize += (uint64)calcSampleCount(i + 1) * sizeof(SpecularItem);
	}

	Image::CopyImageRegion imageCopyRegion;
	imageCopyRegion.layerCount = 6;
	Image::copy(cubemap, specular, imageCopyRegion);

	auto pipelineView = graphicsSystem->get(iblSpecularPipeline);
	auto pushConstants = pipelineView->getPushConstants<PbrLightingRenderSystem::SpecularPC>();
	pipelineView->bind();

	cubemapSize /= 2;
	for (uint8 i = 0; i < (uint32)gpuSpecularCaches.size(); i++)
	{
		auto iblSpecularView = graphicsSystem->createImageView(specular,
			Image::Type::Texture2DArray, cubemapFormat, i + 1, 1, 0, 6);
		auto gpuSpecularCache = gpuSpecularCaches[i];
		map<string, DescriptorSet::Uniform> iblSpecularUniforms =
		{
			{ "cubemap", DescriptorSet::Uniform(defaultCubemapView) },
			{ "specular", DescriptorSet::Uniform(iblSpecularView) },
			{ "cache", DescriptorSet::Uniform(gpuSpecularCache) }
		};
		auto iblSpecularDescriptorSet = graphicsSystem->createDescriptorSet(
			iblSpecularPipeline, std::move(iblSpecularUniforms));
		SET_RESOURCE_DEBUG_NAME(iblSpecularDescriptorSet,
			"descriptorSet.lighting.iblSpecular" + to_string(*iblSpecularDescriptorSet));
		pipelineView->bindDescriptorSet(iblSpecularDescriptorSet);

		pushConstants->imageSize = cubemapSize;
		pushConstants->itemCount = countBuffer[i];
		pipelineView->pushConstants();

		pipelineView->dispatch(uint3(cubemapSize, cubemapSize, 6));

		graphicsSystem->destroy(iblSpecularDescriptorSet);
		graphicsSystem->destroy(iblSpecularView);
		graphicsSystem->destroy(gpuSpecularCache);
		cubemapSize /= 2;
	}

	graphicsSystem->destroy(cpuSpecularCache);
	return specular;
}

//**********************************************************************************************************************
void PbrLightingRenderSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
	Ref<Buffer>& sh, Ref<Image>& specular, Memory::Strategy strategy)
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

	auto graphicsSystem = GraphicsSystem::Instance::get();
	graphicsSystem->startRecording(CommandBufferType::Graphics);

	cubemap = Ref<Image>(graphicsSystem->createImage(Image::Type::Cubemap,
		Image::Format::SfloatR16G16B16A16, Image::Bind::TransferDst | Image::Bind::TransferSrc |
		Image::Bind::Sampled, mips, uint3(size, 1), strategy, Image::Format::SfloatR32G32B32A32));
	SET_RESOURCE_DEBUG_NAME(cubemap, "image.cubemap." + path.generic_string());

	auto cubemapView = graphicsSystem->get(cubemap);
	cubemapView->generateMips();

	auto threadSystem = ThreadSystem::Instance::tryGet();
	sh = Ref<Buffer>(generateIblSH(threadSystem, mips[0], cubemapSize, strategy));
	SET_RESOURCE_DEBUG_NAME(sh, "buffer.sh." + path.generic_string());

	specular = Ref<Image>(generateIblSpecular(threadSystem, 
		iblSpecularPipeline, ID<Image>(cubemap), strategy));
	SET_RESOURCE_DEBUG_NAME(specular, "image.cubemap.specular." + path.generic_string());
	
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
Ref<DescriptorSet> PbrLightingRenderSystem::createDescriptorSet(ID<Buffer> sh, ID<Image> specular)
{
	GARDEN_ASSERT(sh);
	GARDEN_ASSERT(specular);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto specularView = graphicsSystem->get(specular);

	map<string, DescriptorSet::Uniform> iblUniforms =
	{ 
		{ "data", DescriptorSet::Uniform(sh) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
	};

	auto descritptorSet = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(iblUniforms), 1);
	return Ref<DescriptorSet>(descritptorSet);
}
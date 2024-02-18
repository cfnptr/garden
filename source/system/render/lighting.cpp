//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

/*
#include "garden/system/render/lighting.hpp"
#include "garden/system/render/editor/lighting.hpp"
#include "garden/system/resource.hpp"
#include "math/brdf.hpp"
#include "math/sh.hpp"

#define SPECULAR_SAMPLE_COUNT 1024

using namespace garden;
using namespace math::sh;
using namespace math::ibl;
using namespace math::brdf;

namespace
{
	struct LightingPC final
	{
		float4x4 uvToWorld;
		float4 shadowColor;
	};
	struct SpecularPC final
	{
		uint32 count;
	};

	struct SpecularItem final
	{
		float4 l;
		float4 nolMip;

		SpecularItem(const float3& l, float nol, float mip)
		{
			this->l = float4(l, 0.0f);
			this->nolMip = float4(nol, mip, 0.0f, 0.0f);
		}
		SpecularItem() = default;
	};
}

#if 0 // Used to precompute Ki coefs.
//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
static void computeKi()
{
	double ki[SH_COEF_COUNT];

	for (uint32 l = 0; l < SH_BAND_COUNT; l++)
	{
		ki[shIndex(0, l)] = computeKml(0, l);

		for (uint32 m = 1; m <= l; m++)
			ki[shIndex(m, l)] = ki[shIndex(-m, l)] = M_SQRT2 * computeKml(m, l);
	}

	for (uint32 l = 0; l < SH_BAND_COUNT; l++)
	{
		auto truncatedCosSh = computeTruncatedCosSh(l);
		ki[shIndex(0, l)] *= truncatedCosSh;

		for (uint32 m = 1; m <= l; m++)
		{
			ki[shIndex(-m, l)] *= truncatedCosSh;
			ki[shIndex( m, l)] *= truncatedCosSh;
		}
	}

	for (uint32 i = 0; i < SH_COEF_COUNT; i++)
		cout << i << ". " << setprecision(36) << ki[i] << "\n";
}
#endif

//--------------------------------------------------------------------------------------------------
static float pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}
static float log4(float x)
{
    return log2(x) * 0.5f;
}

static float smithGGXCorrelated(float nov, float nol, float a)
{
	auto a2 = a * a;
	auto lambdaV = nol * sqrt((nov - nov * a2) * nov + a2);
    auto lambdaL = nov * sqrt((nol - nol * a2) * nol + a2);
    return 0.5f / (lambdaV + lambdaL);
}
static float2 dfvMultiscatter(uint32 x, uint32 y)
{
	auto nov = clamp((x + 0.5f) / IBL_DFG_SIZE, 0.0f, 1.0f);
	auto coord = clamp((IBL_DFG_SIZE - y + 0.5f) / IBL_DFG_SIZE, 0.0f, 1.0f);
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

static float mipToLinearRoughness(uint8 lodCount, uint8 mip)
{
    const auto a = 2.0f, b = -1.0f;
	auto lod = clamp((float)mip / (lodCount - 1), 0.0f, 1.0f);
    auto perceptualRoughness = clamp((sqrt(
		a * a + 4.0f * b * lod) - a) / (2.0f * b), 0.0f, 1.0f);
	return perceptualRoughness * perceptualRoughness;
}

static uint32 calcSampleCount(uint8 mipLevel)
{
	return SPECULAR_SAMPLE_COUNT * (uint32)exp2((float)std::max(mipLevel - 1, 0));
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createShadowBuffer(GraphicsSystem* graphicsSystem,
	DeferredRenderSystem* deferredSystem, ID<ImageView>* shadowImageViews)
{
	Image::Mips mips(1); mips[0].assign(SHADOW_BUFFER_COUNT, nullptr);
	auto image = graphicsSystem->createImage(Image::Format::UnormR8,
		Image::Bind::ColorAttachment | Image::Bind::Sampled |
		Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		deferredSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.shadow.buffer");

	for (uint32 i = 0; i < SHADOW_BUFFER_COUNT; i++)
	{
		shadowImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, shadowImageViews[i],
			"imageView.lighting.shadow" + to_string(i));
	}

	return image;
}
static void destroyShadowBuffer(GraphicsSystem* graphicsSystem,
	ID<Image> shadowBuffer, ID<ImageView>* shadowImageViews)
{
	for (uint32 i = 0; i < SHADOW_BUFFER_COUNT; i++)
	{
		graphicsSystem->destroy(shadowImageViews[i]);
		shadowImageViews[i] = {};
	}
	graphicsSystem->destroy(shadowBuffer);
}

static void createShadowFramebuffers(
	ID<Framebuffer>* shadowFramebuffers, GraphicsSystem* graphicsSystem,
	DeferredRenderSystem* deferredSystem, const ID<ImageView>* shadowImageViews)
{
	auto framebufferSize = deferredSystem->getFramebufferSize();
	for (uint32 i = 0; i < SHADOW_BUFFER_COUNT; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(shadowImageViews[i], true, false, true) };
		shadowFramebuffers[i] = graphicsSystem->createFramebuffer(
			framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, shadowFramebuffers[i],
			"framebuffer.lighting.shadow" + to_string(i));
	}
}
static void destroyShadowFramebuffers(GraphicsSystem* graphicsSystem,
	ID<Framebuffer>* shadowFramebuffers)
{
	for (uint32 i = 0; i < SHADOW_BUFFER_COUNT; i++)
	{
		graphicsSystem->destroy(shadowFramebuffers[i]);
		shadowFramebuffers[i] = {};
	}
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createAoBuffer(GraphicsSystem* graphicsSystem,
	DeferredRenderSystem* deferredSystem, ID<ImageView>* aoImageViews)
{
	Image::Mips mips(1); mips[0].assign(AO_BUFFER_COUNT, nullptr);
	auto image = graphicsSystem->createImage(Image::Format::UnormR8,
		Image::Bind::ColorAttachment | Image::Bind::Sampled |
		Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		deferredSystem->getFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.ao.buffer");

	for (uint32 i = 0; i < AO_BUFFER_COUNT; i++)
	{
		aoImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoImageViews[i],
			"imageView.lighting.ao" + to_string(i));
	}

	return image;
}
static void destroyAoBuffer(GraphicsSystem* graphicsSystem,
	ID<Image> aoBuffer, ID<ImageView>* aoImageViews)
{
	for (uint32 i = 0; i < AO_BUFFER_COUNT; i++)
	{
		graphicsSystem->destroy(aoImageViews[i]);
		aoImageViews[i] = {};
	}
	graphicsSystem->destroy(aoBuffer);
}

static void createAoFramebuffers(
	ID<Framebuffer>* aoFramebuffers, GraphicsSystem* graphicsSystem,
	DeferredRenderSystem* deferredSystem, const ID<ImageView>* aoImageViews)
{
	auto framebufferSize = deferredSystem->getFramebufferSize();
	for (uint32 i = 0; i < AO_BUFFER_COUNT; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(aoImageViews[i], true, false, true) };
		aoFramebuffers[i] = graphicsSystem->createFramebuffer(
			framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoFramebuffers[i],
			"framebuffer.lighting.ao" + to_string(i));
	}
}
static void destroyAoFramebuffers(GraphicsSystem* graphicsSystem, ID<Framebuffer>* aoFramebuffers)
{
	for (uint32 i = 0; i < AO_BUFFER_COUNT; i++)
	{
		graphicsSystem->destroy(aoFramebuffers[i]);
		aoFramebuffers[i] = {};
	}
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getLightingUniforms(Manager* manager,
	GraphicsSystem* graphicsSystem, DeferredRenderSystem* deferredSystem,
	ID<Image> dfgLUT, ID<ImageView> shadowImageViews[SHADOW_BUFFER_COUNT],
	ID<ImageView> aoImageViews[AO_BUFFER_COUNT])
{
	auto gFramebufferView = graphicsSystem->get(deferredSystem->getGFramebuffer());
	auto& colorAttachments = gFramebufferView->getColorAttachments();
	auto depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBuffer0", DescriptorSet::Uniform(colorAttachments[0].imageView) },
		{ "gBuffer1", DescriptorSet::Uniform(colorAttachments[1].imageView) },
		{ "gBuffer2", DescriptorSet::Uniform(colorAttachments[2].imageView) },
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0] ? // TODO: [1]
			shadowImageViews[0] : graphicsSystem->getWhiteTexture()) },
		{ "aoBuffer", DescriptorSet::Uniform(aoImageViews[1] ?
			aoImageViews[1] : graphicsSystem->getWhiteTexture()) },
		{ "dfgLUT", DescriptorSet::Uniform(graphicsSystem->get(dfgLUT)->getDefaultView()) }
	};

	return uniforms;
}

static map<string, DescriptorSet::Uniform> getShadowDenoiseUniforms(
	ID<ImageView> shadowImageViews[SHADOW_BUFFER_COUNT])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0]) } };
	return uniforms;
}
static map<string, DescriptorSet::Uniform> getAoDenoiseUniforms(
	ID<ImageView> aoImageViews[AO_BUFFER_COUNT])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "aoBuffer", DescriptorSet::Uniform(aoImageViews[0]) } };
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createLightingPipeline(
	DeferredRenderSystem* deferredSystem, bool useShadowBuffer, bool useAoBuffer)
{
	map<string, Pipeline::SpecConst> specConsts =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConst(useShadowBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConst(useAoBuffer) }
	};

	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"pbr-lighting", deferredSystem->getHdrFramebuffer(), 
		deferredSystem->isRenderAsync(), false, 0, 0, specConsts);
}
static ID<ComputePipeline> createIblSpecularPipeline()
{
	return ResourceSystem::getInstance()->loadComputePipeline(
		"ibl-specular", false, false);
}
static ID<GraphicsPipeline> createAoDenoisePipeline(
	const ID<Framebuffer> aoFramebuffers[AO_BUFFER_COUNT])
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"denoise/ao", aoFramebuffers[1]);
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createDfgLUT(Manager* manager, GraphicsSystem* graphicsSystem)
{
	auto pixels = malloc<float2>(IBL_DFG_SIZE * IBL_DFG_SIZE);

	// TODO: check if release build DFG image looks the same as debug.
	auto threadSystem = manager->tryGet<ThreadSystem>();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems(ThreadPool::Task([](const ThreadPool::Task& task)
		{
			auto pixels = (float2*)task.getArgument();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto y = i / IBL_DFG_SIZE, x = i - y * IBL_DFG_SIZE;
				pixels[i] = dfvMultiscatter(x, (IBL_DFG_SIZE - 1) - y);
			}
		},
		pixels), IBL_DFG_SIZE * IBL_DFG_SIZE);
		threadPool.wait();
	}
	else
	{
		auto pixelData = (float2*)pixels; uint32 index = 0;
		for (int32 y = IBL_DFG_SIZE - 1; y >= 0; y--)
		{
			for (uint32 x = 0; x < IBL_DFG_SIZE; x++)
				pixelData[index++] = dfvMultiscatter(x, y);
		}
	}
	
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16,
		Image::Bind::TransferDst | Image::Bind::Sampled, { { pixels } },
		int2(IBL_DFG_SIZE), Image::Strategy::Size, Image::Format::SfloatR32G32);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.dfgLUT");
	free(pixels);
	return image;
}

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::initialize()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	auto deferredSystem = getDeferredSystem();

	if (!dfgLUT)
		dfgLUT = createDfgLUT(manager, graphicsSystem);

	if (useShadowBuffer)
	{
		if (!shadowBuffer)
			shadowBuffer = createShadowBuffer(graphicsSystem, deferredSystem, shadowImageViews);

		if (!shadowFramebuffers[0])
		{
			createShadowFramebuffers(shadowFramebuffers,
				graphicsSystem, deferredSystem, shadowImageViews);
		}
	}
	if (useAoBuffer)
	{
		if (!aoBuffer)
			aoBuffer = createAoBuffer(graphicsSystem, deferredSystem, aoImageViews);

		if (!aoFramebuffers[0])
		{
			createAoFramebuffers(aoFramebuffers,
				graphicsSystem, deferredSystem, aoImageViews);
		}

		if (!aoDenoisePipeline)
			aoDenoisePipeline = createAoDenoisePipeline(aoFramebuffers);
	}	

	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(deferredSystem, useShadowBuffer, useAoBuffer);
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();

	auto& subsystems = getManager()->getSubsystems<LightingRenderSystem>();
	for (auto subsystem : subsystems)
	{
		auto shadowSystem = dynamic_cast<IShadowRenderSystem*>(subsystem.system);
		if (shadowSystem)
		{
			shadowSystem->lightingSystem = this;
			continue;
		}

		auto aoSystem = dynamic_cast<IAoRenderSystem*>(subsystem.system);
		if (aoSystem)
			aoSystem->lightingSystem = this;
	}

	#if GARDEN_EDITOR
	editor = new LightingEditor(this);
	#endif
}
void LightingRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (LightingEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::preHdrRender()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	if (!graphicsSystem->camera)
		return;

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	if (pipelineView->isReady() && !lightingDescriptorSet)
	{
		auto uniforms = getLightingUniforms(manager, graphicsSystem,
			getDeferredSystem(), dfgLUT, shadowImageViews, aoImageViews);
		lightingDescriptorSet = graphicsSystem->createDescriptorSet(
			lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, lightingDescriptorSet,
			"descriptorSet.lighting.base");
	}

	auto& subsystems = manager->getSubsystems<LightingRenderSystem>();
	auto hasAnyShadow = false, hasAnyAO = false;

	if (!subsystems.empty())
	{
		{
			SET_GPU_DEBUG_LABEL("Pre Shadow Pass", Color::transparent);
			for (auto subsystem : subsystems)
			{
				auto shadowSystem = dynamic_cast<IShadowRenderSystem*>(subsystem.system);
				if (!shadowSystem)
					continue;
				shadowSystem->preShadowRender();
			}
		}
		{
			SET_GPU_DEBUG_LABEL("Pre AO Pass", Color::transparent);
			for (auto subsystem : subsystems)
			{
				auto aoSystem = dynamic_cast<IAoRenderSystem*>(subsystem.system);
				if (!aoSystem)
					continue;
				aoSystem->preAoRender();
			}
		}

		auto deferredSystem = getDeferredSystem();
		if (useShadowBuffer)
		{
			if (!shadowBuffer)
				shadowBuffer = createShadowBuffer(graphicsSystem, deferredSystem, shadowImageViews);
			if (!shadowFramebuffers[0])
			{
				createShadowFramebuffers(shadowFramebuffers,
					graphicsSystem, deferredSystem, shadowImageViews);
			}

			SET_GPU_DEBUG_LABEL("Shadow Pass", Color::transparent);
			auto framebufferView = graphicsSystem->get(shadowFramebuffers[0]);
			framebufferView->beginRenderPass(float4(1.0f));

			for (auto subsystem : subsystems)
			{
				auto shadowSystem = dynamic_cast<IShadowRenderSystem*>(subsystem.system);
				if (!shadowSystem)
					continue;
				hasAnyShadow |= shadowSystem->shadowRender();
			}

			framebufferView->endRenderPass();
		}
		if (useAoBuffer)
		{
			if (!aoBuffer)
				aoBuffer = createAoBuffer(graphicsSystem, deferredSystem, aoImageViews);
			if (!aoFramebuffers[0])
			{
				createAoFramebuffers(aoFramebuffers,
					graphicsSystem, deferredSystem, aoImageViews);
			}
			if (!aoDenoisePipeline)
				aoDenoisePipeline = createAoDenoisePipeline(aoFramebuffers);

			SET_GPU_DEBUG_LABEL("AO Pass", Color::transparent);
			auto framebufferView = graphicsSystem->get(aoFramebuffers[0]);
			framebufferView->beginRenderPass(float4(1.0f));

			for (auto subsystem : subsystems)
			{
				auto aoSystem = dynamic_cast<IAoRenderSystem*>(subsystem.system);
				if (!aoSystem)
					continue;
				hasAnyAO |= aoSystem->aoRender();
			}

			framebufferView->endRenderPass();
		}
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
				aoDenoiseDescriptorSet = graphicsSystem->createDescriptorSet(
					aoDenoisePipeline, std::move(uniforms));
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoDenoiseDescriptorSet,
					"descriptorSet.lighting.ao-denoise");
			}

			SET_GPU_DEBUG_LABEL("AO Denoise Pass", Color::transparent);
			auto framebufferView = graphicsSystem->get(aoFramebuffers[1]);
			framebufferView->beginRenderPass(float4(1.0f));
			aoPipelineView->bind();
			aoPipelineView->setViewportScissor(float4(
				float2(0), framebufferView->getSize()));
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

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::hdrRender()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(lightingPipeline);
	auto dfgLutView = graphicsSystem->get(dfgLUT);
	if (!graphicsSystem->camera || !pipelineView->isReady() ||
		!dfgLutView->isReady() || !lightingDescriptorSet)
	{
		return;
	}

	auto lightingComponent = manager->tryGet<LightingRenderComponent>(graphicsSystem->camera);
	if (!lightingComponent || !lightingComponent->cubemap ||
		!lightingComponent->sh || !lightingComponent->specular)
	{
		return;
	}

	auto cubemapView = graphicsSystem->get(lightingComponent->cubemap);
	auto shView = graphicsSystem->get(lightingComponent->sh);
	auto specularView = graphicsSystem->get(lightingComponent->specular);
	if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
		return;
	
	if (!lightingComponent->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet(
			lightingComponent->sh, lightingComponent->specular);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
			"descriptorSet.lighting" + to_string(*descriptorSet));
		lightingComponent->descriptorSet = descriptorSet;
	}

	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	const auto uvToNDC = float4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	SET_GPU_DEBUG_LABEL("PBR Lighting", Color::transparent);
	Pipeline::DescriptorData descriptorData[2];
	descriptorData[0] = Pipeline::DescriptorData(lightingDescriptorSet);
	descriptorData[1] = Pipeline::DescriptorData(lightingComponent->descriptorSet);

	auto deferredSystem = getDeferredSystem();
	if (deferredSystem->isRenderAsync())
	{
		pipelineView->bindAsync(0, 0);
		pipelineView->setViewportScissorAsync(float4(float2(0),
			deferredSystem->getFramebufferSize()), 0);
		pipelineView->bindDescriptorSetsAsync(descriptorData, 2, 0);
		auto pushConstants = pipelineView->getPushConstantsAsync<LightingPC>(0);
		pushConstants->uvToWorld = cameraConstants.viewProjInv * uvToNDC;
		pushConstants->shadowColor = float4((float3)shadowColor * shadowColor.w, 0.0f);
		pipelineView->pushConstantsAsync(0);
		pipelineView->drawFullscreenAsync(0);
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor(float4(float2(0),
			deferredSystem->getFramebufferSize()));
		pipelineView->bindDescriptorSets(descriptorData, 2);
		auto pushConstants = pipelineView->getPushConstants<LightingPC>();
		pushConstants->uvToWorld = cameraConstants.viewProjInv * uvToNDC;
		pushConstants->shadowColor = float4((float3)shadowColor * shadowColor.w, 0.0f);
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
	}
}

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.framebufferSize)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto deferredSystem = getDeferredSystem();
		auto framebufferSize = deferredSystem->getFramebufferSize();

		if (aoBuffer)
		{
			destroyAoBuffer(graphicsSystem, aoBuffer, aoImageViews);
			aoBuffer = createAoBuffer(graphicsSystem, deferredSystem, aoImageViews);
		}
		if (aoFramebuffers[0])
		{
			for (uint32 i = 0; i < AO_BUFFER_COUNT; i++)
			{
				auto framebufferView = graphicsSystem->get(aoFramebuffers[i]);
				Framebuffer::OutputAttachment colorAttachment(
					aoImageViews[i], true, false, true);
				framebufferView->update(framebufferSize, &colorAttachment, 1);
			}
		}

		if (shadowBuffer)
		{
			destroyShadowBuffer(graphicsSystem, shadowBuffer, shadowImageViews);
			shadowBuffer = createShadowBuffer(graphicsSystem, deferredSystem, shadowImageViews);
		}
		if (shadowFramebuffers[0])
		{
			for (uint32 i = 0; i < SHADOW_BUFFER_COUNT; i++)
			{
				auto framebufferView = graphicsSystem->get(shadowFramebuffers[i]);
				Framebuffer::OutputAttachment colorAttachment(
					shadowImageViews[i], true, false, true);
				framebufferView->update(framebufferSize, &colorAttachment, 1);
			}
		}

		if (lightingDescriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(lightingDescriptorSet);
			auto uniforms = getLightingUniforms(getManager(), graphicsSystem,
				getDeferredSystem(), dfgLUT, shadowImageViews, aoImageViews);
			descriptorSetView->recreate(std::move(uniforms));
		}
		if (aoDenoiseDescriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(aoDenoiseDescriptorSet);
			auto uniforms = getAoDenoiseUniforms(aoImageViews);
			descriptorSetView->recreate(std::move(uniforms));
		}
	}
}

//--------------------------------------------------------------------------------------------------
type_index LightingRenderSystem::getComponentType() const
{
	return typeid(LightingRenderComponent);
}
ID<Component> LightingRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<CameraComponent>(entity));
	return ID<Component>(components.create());
}
void LightingRenderSystem::destroyComponent(ID<Component> instance)
{
	auto graphicsSystem = getGraphicsSystem();
	auto component = components.get(ID<LightingRenderComponent>(instance));
	if (component->cubemap.getRefCount() == 1)
		graphicsSystem->destroy(component->cubemap);
	if (component->sh.getRefCount() == 1)
		graphicsSystem->destroy(component->cubemap);
	if (component->specular.getRefCount() == 1)
		graphicsSystem->destroy(component->cubemap);
	if (component->descriptorSet.getRefCount() == 1)
		graphicsSystem->destroy(component->cubemap);
	components.destroy(ID<LightingRenderComponent>(instance));
}
View<Component> LightingRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<LightingRenderComponent>(instance)));
}
void LightingRenderSystem::disposeComponents() { components.dispose(); }

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::setConsts(bool useShadowBuffer, bool useAoBuffer)
{
	if (this->useShadowBuffer == useShadowBuffer && this->useAoBuffer == useAoBuffer)
		return;

	this->useShadowBuffer = useShadowBuffer; this->useAoBuffer = useAoBuffer;
	if (!lightingPipeline)
		return;

	auto graphicsSystem = getGraphicsSystem();
	auto deferredSystem = getDeferredSystem();
	graphicsSystem->destroy(lightingDescriptorSet);
	lightingDescriptorSet = {};

	auto lightingComponents = components.getData();
	auto lightingOccupancy = components.getOccupancy();
	for (uint32 i = 0; i < lightingOccupancy; i++)
	{
		graphicsSystem->destroy(lightingComponents[i].descriptorSet);
		lightingComponents[i].descriptorSet = {};
	}

	if (this->useShadowBuffer != useShadowBuffer)
	{
		if (useShadowBuffer)
		{
			shadowBuffer = createShadowBuffer(graphicsSystem, deferredSystem, shadowImageViews);
			createShadowFramebuffers(shadowFramebuffers,
				graphicsSystem, deferredSystem, shadowImageViews);
		}
		else
		{
			destroyShadowFramebuffers(graphicsSystem, shadowFramebuffers);
			destroyShadowBuffer(graphicsSystem, shadowBuffer, shadowImageViews);
			shadowBuffer = {};
		}
	}
	if (this->useAoBuffer != useAoBuffer)
	{
		if (useAoBuffer)
		{
			aoBuffer = createAoBuffer(graphicsSystem, deferredSystem, aoImageViews);
			createAoFramebuffers(aoFramebuffers, graphicsSystem, deferredSystem, aoImageViews);
		}
		else
		{
			destroyAoFramebuffers(graphicsSystem, aoFramebuffers);
			destroyAoBuffer(graphicsSystem, aoBuffer, aoImageViews);
			aoBuffer = {};
		}
	}

	graphicsSystem->destroy(lightingPipeline);
	lightingPipeline = createLightingPipeline(getDeferredSystem(), useShadowBuffer, useAoBuffer);
}

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> LightingRenderSystem::getLightingPipeline()
{
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(getDeferredSystem(), useShadowBuffer, useAoBuffer);
	return lightingPipeline;
}
ID<ComputePipeline> LightingRenderSystem::getIblSpecularPipeline()
{
	if (!iblSpecularPipeline)
		iblSpecularPipeline = createIblSpecularPipeline();
	return iblSpecularPipeline;
}
ID<GraphicsPipeline> LightingRenderSystem::getAoDenoisePipeline()
{
	if (!aoDenoisePipeline)
		aoDenoisePipeline = createAoDenoisePipeline(getAoFramebuffers());
	return aoDenoisePipeline;
}

const ID<Framebuffer>* LightingRenderSystem::getShadowFramebuffers()
{
	if (!shadowFramebuffers[0])
	{
		createShadowFramebuffers(shadowFramebuffers, getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), getShadowImageViews());
	}
	return shadowFramebuffers;
}
const ID<Framebuffer>* LightingRenderSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0])
	{
		createAoFramebuffers(aoFramebuffers, getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), getAoImageViews());
	}
	return aoFramebuffers;
}

ID<Image> LightingRenderSystem::getDfgLUT()
{
	if (!dfgLUT)
		dfgLUT = createDfgLUT(getManager(), getGraphicsSystem());
	return dfgLUT;
}
ID<Image> LightingRenderSystem::getShadowBuffer()
{
	if (!shadowBuffer)
	{
		shadowBuffer = createShadowBuffer(getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), shadowImageViews);
	}
	return shadowBuffer;
}
ID<Image> LightingRenderSystem::getAoBuffer()
{
	if (!aoBuffer)
	{
		aoBuffer = createAoBuffer(getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), aoImageViews);
	}
	return aoBuffer;
}
const ID<ImageView>* LightingRenderSystem::getShadowImageViews()
{
	if (!shadowBuffer)
	{
		shadowBuffer = createShadowBuffer(getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), shadowImageViews);
	}
	return shadowImageViews;
}
const ID<ImageView>* LightingRenderSystem::getAoImageViews()
{
	if (!aoBuffer)
	{
		aoBuffer = createAoBuffer(getGraphicsSystem(),
			getManager()->get<DeferredRenderSystem>(), aoImageViews);
	}
	return aoImageViews;
}

//--------------------------------------------------------------------------------------------------
static ID<Buffer> generateIblSH(GraphicsSystem* graphicsSystem, ThreadPool& threadPool,
	const vector<const void*>& _pixels, uint32 cubemapSize, Buffer::Strategy strategy)
{
	auto invDim = 1.0f / cubemapSize;
	vector<float4> shBuffer(threadPool.getThreadCount() * SH_COEF_COUNT);
	auto faces = (const float4**)_pixels.data();

	threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
	{
		auto sh = (float4*)task.getArgument() + task.getTaskIndex() * SH_COEF_COUNT;
		auto itemCount = task.getItemCount(), itemOffset = task.getItemOffset();
		auto invCubemapSize = cubemapSize - 1;
		float shb[SH_COEF_COUNT];

		for (uint8 face = 0; face < 6; face++)
		{
			auto pixels = faces[face];
			for (uint32 i = itemOffset; i < itemCount; i++)
			{
				auto y = i / cubemapSize, x = i - y * cubemapSize;
				auto st = coordsToST(int2(x, y), invDim);
				auto dir = stToDir(st, face);
				// TODO: check if inversion is required, or just use pixels[i].
				auto color = pixels[(invCubemapSize - y) * cubemapSize + x];
				color *= calcSolidAngle(st, invDim);
				computeShBasis(dir, shb);

				for (uint32 j = 0; j < SH_COEF_COUNT; j++)
					sh[j] += color * shb[j];
			}
		}
	},
	shBuffer.data()), cubemapSize * cubemapSize);
	threadPool.wait();

	float4 shData[SH_COEF_COUNT];
	for (uint32 i = 0; i < SH_COEF_COUNT; i++)
		shData[i] = 0.0f;

	for (uint32 i = 0; i < threadPool.getThreadCount(); i++)
	{
		auto data = shBuffer.data() + i * SH_COEF_COUNT;
		for (uint32 j = 0; j < SH_COEF_COUNT; j++)
			shData[j] += data[j];
	}

	for (uint32 i = 0; i < SH_COEF_COUNT; i++)
		shData[i] *= ki[i];
	
	deringingSH(shData);
	shaderPreprocessSH(shData);

	// TODO: check if final SH is the same as debug in release build.
	return graphicsSystem->createBuffer(
		Buffer::Bind::TransferDst | Buffer::Bind::Uniform, Buffer::Access::None,
		shData, SH_COEF_COUNT * sizeof(float4), Buffer::Usage::PreferGPU, strategy);
}

//--------------------------------------------------------------------------------------------------
static ID<Image> generateIblSpecular(GraphicsSystem* graphicsSystem, ThreadPool& threadPool,
	ID<ComputePipeline> iblSpecularPipeline, ID<Image> cubemap, Memory::Strategy strategy)
{
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
		Image::Format::SfloatR16G16B16A16, Image::Bind::TransferDst |
		Image::Bind::Storage | Image::Bind::Sampled, mips,
		int3(cubemapSize, cubemapSize, 1), strategy);

	uint64 specularCacheSize = 0;
	for (uint8 i = 1; i < specularMipCount; i++)
		specularCacheSize += calcSampleCount(i);
	specularCacheSize *= sizeof(SpecularItem);

	auto cpuSpecularCache = graphicsSystem->createBuffer(
		Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
		specularCacheSize, Buffer::Usage::Auto, Buffer::Strategy::Speed);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, cpuSpecularCache,
		"buffer.storage.lighting.cpuSpecularCache" + to_string(*cpuSpecularCache));
	auto cpuSpecularCacheView = graphicsSystem->get(cpuSpecularCache);

	vector<uint32> countBuffer(specularMipCount - 1);
	vector<ID<Buffer>> gpuSpecularCaches(countBuffer.size());

	threadPool.addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
	{
		psize mapOffset = 0;
		auto mipIndex = task.getTaskIndex() + 1;
		for (uint8 i = 1; i < mipIndex; i++)
			mapOffset += calcSampleCount(i);
		auto map = (SpecularItem*)task.getArgument() + mapOffset;

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
				const auto k = 1.0f; // log4(4.0f);
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

		countBuffer[task.getTaskIndex()] = count;
	},
	cpuSpecularCacheView->getMap()), (uint32)countBuffer.size());
	threadPool.wait();

	SET_GPU_DEBUG_LABEL("IBL Specular Generation", Color::transparent);
	cpuSpecularCacheView->flush();
		
	specularCacheSize = 0;
	for (uint32 i = 0; i < (uint32)gpuSpecularCaches.size(); i++)
	{
		auto cacheSize = countBuffer[i] * sizeof(SpecularItem);
		auto gpuSpecularCache = graphicsSystem->createBuffer(Buffer::Bind::Storage |
			Buffer::Bind::TransferDst, Buffer::Access::None, cacheSize,
			Buffer::Usage::PreferGPU, Buffer::Strategy::Speed);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, gpuSpecularCache,
			"buffer.storage.lighting.gpuSpecularCache" + to_string(*gpuSpecularCache));
		gpuSpecularCaches[i] = gpuSpecularCache;

		Buffer::CopyRegion bufferCopyRegion;
		bufferCopyRegion.size = cacheSize;
		bufferCopyRegion.srcOffset = specularCacheSize;
		Buffer::copy(cpuSpecularCache, gpuSpecularCache, bufferCopyRegion);
		specularCacheSize += calcSampleCount(i + 1) * sizeof(SpecularItem);
	}

	Image::CopyImageRegion imageCopyRegion;
	imageCopyRegion.extent = int3(cubemapSize, cubemapSize, 1);
	imageCopyRegion.layerCount = 6;
	Image::copy(cubemap, specular, imageCopyRegion);

	auto pipelineView = graphicsSystem->get(iblSpecularPipeline);
	pipelineView->bind();

	cubemapSize /= 2;
	for (uint8 i = 1; i < specularMipCount; i++)
	{
		auto iblSpecularView = graphicsSystem->createImageView(specular,
			Image::Type::Texture2DArray, cubemapFormat, i, 1, 0, 6);
		auto gpuSpecularCache = gpuSpecularCaches[i - 1];
		map<string, DescriptorSet::Uniform> iblSpecularUniforms =
		{
			{ "cubemap", DescriptorSet::Uniform(defaultCubemapView) },
			{ "specular", DescriptorSet::Uniform(iblSpecularView) },
			{ "cache", DescriptorSet::Uniform(gpuSpecularCache) }
		};
		auto iblSpecularDescriptorSet = graphicsSystem->createDescriptorSet(
			iblSpecularPipeline, std::move(iblSpecularUniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, iblSpecularDescriptorSet,
			"descriptorSet.lighting.iblSpecular" + to_string(*iblSpecularDescriptorSet));
		pipelineView->bindDescriptorSet(iblSpecularDescriptorSet);

		auto pushConstants = pipelineView->getPushConstants<SpecularPC>();
		pushConstants->count = countBuffer[i - 1];
		pipelineView->pushConstants();
		pipelineView->dispatch(int3(cubemapSize, cubemapSize, 6));

		graphicsSystem->destroy(iblSpecularDescriptorSet);
		graphicsSystem->destroy(iblSpecularView);
		graphicsSystem->destroy(gpuSpecularCache);
		cubemapSize /= 2;
	}

	graphicsSystem->destroy(cpuSpecularCache);
	return specular;
}

//--------------------------------------------------------------------------------------------------
void LightingRenderSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
	Ref<Buffer>& sh, Ref<Image>& specular, Memory::Strategy strategy)
{
	GARDEN_ASSERT(!path.empty());
	auto graphicsSystem = getGraphicsSystem();
	// TODO: rewrite with tryGet. Propagate to generateIblSH and generateIblSpecular.
	auto threadSystem = getManager()->get<ThreadSystem>();
	auto& threadPool = threadSystem->getForegroundPool();

	vector<uint8> left, right, bottom, top, back, front;
	int2 size; Image::Format format;
	ResourceSystem::getInstance()->loadCubemapData(path,
		left, right, bottom, top, back, front, size, format);
	auto cubemapSize = size.x;

	auto mipCount = calcMipCount(cubemapSize);
	Image::Mips mips(mipCount);
	mips[0] = { right.data(), left.data(), top.data(),
		bottom.data(), front.data(), back.data() };
	for (uint8 i = 1; i < mipCount; i++)
		mips[i] = Image::Layers(6);

	graphicsSystem->startRecording(CommandBufferType::Graphics);

	cubemap = graphicsSystem->createImage(Image::Type::Cubemap,
		Image::Format::SfloatR16G16B16A16, Image::Bind::TransferDst | Image::Bind::TransferSrc |
		Image::Bind::Sampled, mips, int3(size, 1), strategy, format);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, cubemap, "image.cubemap." + path.generic_string());

	auto cubemapView = graphicsSystem->get(cubemap);
	cubemapView->generateMips();

	sh = generateIblSH(graphicsSystem, threadPool, mips[0], cubemapSize, strategy);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, sh, "buffer.sh." + path.generic_string());

	specular = generateIblSpecular(graphicsSystem,
		threadPool, iblSpecularPipeline, cubemap, strategy);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, specular,
		"image.cubemap.specular." + path.generic_string());
	
	graphicsSystem->stopRecording();
}

//--------------------------------------------------------------------------------------------------
Ref<DescriptorSet> LightingRenderSystem::createDescriptorSet(
	ID<Buffer> sh, ID<Image> specular)
{
	auto graphicsSystem = getGraphicsSystem();
	auto specularView = graphicsSystem->get(specular);

	map<string, DescriptorSet::Uniform> iblUniforms =
	{ 
		{ "data", DescriptorSet::Uniform(sh) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
	};

	return graphicsSystem->createDescriptorSet(
		lightingPipeline, std::move(iblUniforms), 1);
}
*/
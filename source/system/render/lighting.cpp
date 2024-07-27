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

#include "garden/system/render/lighting.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/thread.hpp"
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

		SpecularItem(const float3& l, float nol, float mip)  noexcept
		{
			this->l = float4(l, 0.0f);
			this->nolMip = float4(nol, mip, 0.0f, 0.0f);
		}
		SpecularItem() = default;
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
static float pow5(float x) noexcept
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
	const auto a = 2.0f, b = -1.0f;
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
	auto graphicsSystem = GraphicsSystem::getInstance();
	Image::Mips mips(1); mips[0].assign(shadowBufferCount, nullptr);
	auto image = graphicsSystem->createImage(Image::Format::UnormR8, Image::Bind::ColorAttachment |
		Image::Bind::Sampled | Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.shadow.buffer");

	for (uint32 i = 0; i < shadowBufferCount; i++)
	{
		shadowImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, shadowImageViews[i], "imageView.lighting.shadow" + to_string(i));
	}

	return image;
}
static void destroyShadowBuffer(ID<Image> shadowBuffer, ID<ImageView>* shadowImageViews)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	for (uint32 i = 0; i < shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowImageViews[i]);
		shadowImageViews[i] = {};
	}
	graphicsSystem->destroy(shadowBuffer);
}

static void createShadowFramebuffers(ID<Framebuffer>* shadowFramebuffers, const ID<ImageView>* shadowImageViews)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	for (uint32 i = 0; i < shadowBufferCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(shadowImageViews[i], true, false, true) };
		shadowFramebuffers[i] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, shadowFramebuffers[i], "framebuffer.lighting.shadow" + to_string(i));
	}
}
static void destroyShadowFramebuffers(ID<Framebuffer>* shadowFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	for (uint32 i = 0; i < shadowBufferCount; i++)
	{
		graphicsSystem->destroy(shadowFramebuffers[i]);
		shadowFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static ID<Image> createAoBuffer(ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	Image::Mips mips(1); mips[0].assign(aoBufferCount, nullptr);
	auto image = graphicsSystem->createImage(Image::Format::UnormR8, Image::Bind::ColorAttachment |
		Image::Bind::Sampled | Image::Bind::Fullscreen | Image::Bind::TransferDst, mips,
		graphicsSystem->getScaledFramebufferSize(), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.ao.buffer");

	for (uint32 i = 0; i < aoBufferCount; i++)
	{
		aoImageViews[i] = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, Image::Format::UnormR8, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoImageViews[i], "imageView.lighting.ao" + to_string(i));
	}

	return image;
}
static void destroyAoBuffer(ID<Image> aoBuffer, ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	for (uint32 i = 0; i < aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoImageViews[i]);
		aoImageViews[i] = {};
	}
	graphicsSystem->destroy(aoBuffer);
}

static void createAoFramebuffers(ID<Framebuffer>* aoFramebuffers, const ID<ImageView>* aoImageViews)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto framebufferSize = graphicsSystem->getScaledFramebufferSize();
	for (uint32 i = 0; i < aoBufferCount; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments
		{ Framebuffer::OutputAttachment(aoImageViews[i], true, false, true) };
		aoFramebuffers[i] = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoFramebuffers[i], "framebuffer.lighting.ao" + to_string(i));
	}
}
static void destroyAoFramebuffers(ID<Framebuffer>* aoFramebuffers)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	for (uint32 i = 0; i < aoBufferCount; i++)
	{
		graphicsSystem->destroy(aoFramebuffers[i]);
		aoFramebuffers[i] = {};
	}
}

//**********************************************************************************************************************
static map<string, DescriptorSet::Uniform> getLightingUniforms(ID<Image> dfgLUT,
	ID<ImageView> shadowImageViews[shadowBufferCount], ID<ImageView> aoImageViews[aoBufferCount])
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto gFramebufferView = graphicsSystem->get(DeferredRenderSystem::getInstance()->getGFramebuffer());
	const auto& colorAttachments = gFramebufferView->getColorAttachments();
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
	ID<ImageView> shadowImageViews[shadowBufferCount])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "shadowBuffer", DescriptorSet::Uniform(shadowImageViews[0]) } };
	return uniforms;
}
static map<string, DescriptorSet::Uniform> getAoDenoiseUniforms(
	ID<ImageView> aoImageViews[aoBufferCount])
{
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "aoBuffer", DescriptorSet::Uniform(aoImageViews[0]) } };
	return uniforms;
}

static ID<GraphicsPipeline> createLightingPipeline(bool useShadowBuffer, bool useAoBuffer)
{
	auto deferredSystem = DeferredRenderSystem::getInstance();
	map<string, Pipeline::SpecConstValue> specConstValues =
	{
		{ "USE_SHADOW_BUFFER", Pipeline::SpecConstValue(useShadowBuffer) },
		{ "USE_AO_BUFFER", Pipeline::SpecConstValue(useAoBuffer) }
	};

	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"pbr-lighting", deferredSystem->getHdrFramebuffer(), 
		deferredSystem->useAsyncRecording(), false, 0, 0, specConstValues);
}
static ID<ComputePipeline> createIblSpecularPipeline()
{
	return ResourceSystem::getInstance()->loadComputePipeline("ibl-specular", false, false);
}
static ID<GraphicsPipeline> createAoDenoisePipeline(const ID<Framebuffer> aoFramebuffers[aoBufferCount])
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline("denoise/ao", aoFramebuffers[1]);
}

//**********************************************************************************************************************
static ID<Image> createDfgLUT()
{
	auto pixels = malloc<float2>(iblDfgSize * iblDfgSize);

	// TODO: check if release build DFG image looks the same as debug.
	auto threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto y = i / iblDfgSize, x = i - y * iblDfgSize;
				pixels[i] = dfvMultiscatter(x, (iblDfgSize - 1) - y);
			}
		}),
		iblDfgSize * iblDfgSize);
		threadPool.wait();
	}
	else
	{
		auto pixelData = (float2*)pixels; uint32 index = 0;
		for (int32 y = iblDfgSize - 1; y >= 0; y--)
		{
			for (uint32 x = 0; x < iblDfgSize; x++)
				pixelData[index++] = dfvMultiscatter(x, y);
		}
	}
	
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto image = graphicsSystem->createImage(Image::Format::SfloatR16G16, Image::Bind::TransferDst |
		Image::Bind::Sampled, { { pixels } }, int2(iblDfgSize), Image::Strategy::Size, Image::Format::SfloatR32G32);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.lighting.dfgLUT");
	free(pixels);
	return image;
}

//**********************************************************************************************************************
LightingRenderSystem::LightingRenderSystem(bool useShadowBuffer, bool useAoBuffer)
{
	this->hasShadowBuffer = useShadowBuffer;
	this->hasAoBuffer = useAoBuffer;

	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", LightingRenderSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", LightingRenderSystem::deinit);
}
LightingRenderSystem::~LightingRenderSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", LightingRenderSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", LightingRenderSystem::deinit);
	}
}

//**********************************************************************************************************************
void LightingRenderSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<DeferredRenderSystem>());

	SUBSCRIBE_TO_EVENT("PreHdrRender", LightingRenderSystem::preHdrRender);
	SUBSCRIBE_TO_EVENT("HdrRender", LightingRenderSystem::hdrRender);
	SUBSCRIBE_TO_EVENT("GBufferRecreate", LightingRenderSystem::gBufferRecreate);

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
void LightingRenderSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->destroy(iblSpecularPipeline);
		graphicsSystem->destroy(lightingPipeline);
		graphicsSystem->destroy(aoDenoisePipeline);
		destroyAoFramebuffers(aoFramebuffers);
		destroyAoBuffer(aoBuffer, aoImageViews);
		destroyShadowFramebuffers(aoFramebuffers);
		destroyShadowBuffer(shadowBuffer, shadowImageViews);

		SUBSCRIBE_TO_EVENT("PreHdrRender", LightingRenderSystem::preHdrRender);
		SUBSCRIBE_TO_EVENT("HdrRender", LightingRenderSystem::hdrRender);
		SUBSCRIBE_TO_EVENT("GBufferRecreate", LightingRenderSystem::gBufferRecreate);
	}
}

//**********************************************************************************************************************
void LightingRenderSystem::preHdrRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	if (!graphicsSystem->camera)
		return;

	auto manager = Manager::getInstance();
	deferredSystem = manager->get<DeferredRenderSystem>();

	auto pipelineView = graphicsSystem->get(lightingPipeline);
	if (pipelineView->isReady() && !lightingDescriptorSet)
	{
		auto uniforms = getLightingUniforms(dfgLUT, shadowImageViews, aoImageViews);
		lightingDescriptorSet = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, lightingDescriptorSet, "descriptorSet.lighting.base");
	}

	const auto& systems = manager->getSystems();
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
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, aoDenoiseDescriptorSet, "descriptorSet.lighting.ao-denoise");
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
void LightingRenderSystem::hdrRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto pipelineView = graphicsSystem->get(lightingPipeline);
	auto dfgLutView = graphicsSystem->get(dfgLUT);
	if (!graphicsSystem->camera || !pipelineView->isReady() || !dfgLutView->isReady() || !lightingDescriptorSet)
		return;

	auto lightingView = Manager::getInstance()->tryGet<LightingRenderComponent>(graphicsSystem->camera); // TODO: use lightingSystem->tryGet()
	if (!lightingView || !lightingView->cubemap || !lightingView->sh || !lightingView->specular)
		return;

	auto cubemapView = graphicsSystem->get(lightingView->cubemap);
	auto shView = graphicsSystem->get(lightingView->sh);
	auto specularView = graphicsSystem->get(lightingView->specular);
	if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
		return;
	
	if (!lightingView->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet( // TODO: maybe create shared DS?
			ID<Buffer>(lightingView->sh), ID<Image>(lightingView->specular));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet, "descriptorSet.lighting" + to_string(*descriptorSet));
		lightingView->descriptorSet = descriptorSet;
	}

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	const auto uvToNDC = float4x4
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	SET_GPU_DEBUG_LABEL("PBR Lighting", Color::transparent);
	DescriptorSet::Range descriptorSetRange[2];
	descriptorSetRange[0] = DescriptorSet::Range(lightingDescriptorSet);
	descriptorSetRange[1] = DescriptorSet::Range(ID<DescriptorSet>(lightingView->descriptorSet));

	if (deferredSystem->useAsyncRecording())
	{
		pipelineView->bindAsync(0, 0);
		pipelineView->setViewportScissorAsync(float4(0.0f), 0);
		pipelineView->bindDescriptorSetsAsync(descriptorSetRange, 2, 0);
		auto pushConstants = pipelineView->getPushConstantsAsync<LightingPC>(0);
		pushConstants->uvToWorld = cameraConstants.viewProjInv * uvToNDC;
		pushConstants->shadowColor = float4((float3)shadowColor * shadowColor.w, 0.0f);
		pipelineView->pushConstantsAsync(0);
		pipelineView->drawFullscreenAsync(0);
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSets(descriptorSetRange, 2);
		auto pushConstants = pipelineView->getPushConstants<LightingPC>();
		pushConstants->uvToWorld = cameraConstants.viewProjInv * uvToNDC;
		pushConstants->shadowColor = float4((float3)shadowColor * shadowColor.w, 0.0f);
		pipelineView->pushConstants();
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void LightingRenderSystem::gBufferRecreate()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
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
ID<Component> LightingRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(Manager::getInstance()->has<CameraComponent>(entity));
	return ID<Component>(components.create());
}
void LightingRenderSystem::destroyComponent(ID<Component> instance)
{
	auto resourceSystem = ResourceSystem::getInstance();
	auto componentView = components.get(ID<LightingRenderComponent>(instance));
	resourceSystem->destroyShared(componentView->cubemap);
	resourceSystem->destroyShared(componentView->sh);
	resourceSystem->destroyShared(componentView->specular);
	resourceSystem->destroyShared(componentView->descriptorSet);
	components.destroy(ID<LightingRenderComponent>(instance));
}
void LightingRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<LightingRenderComponent>(source);
	auto destinationView = View<LightingRenderComponent>(destination);
	destinationView->cubemap = sourceView->cubemap;
	destinationView->sh = sourceView->sh;
	destinationView->specular = sourceView->specular;
	destinationView->descriptorSet = sourceView->descriptorSet;
	// TODO: destroy destination shared resources
}
const string& LightingRenderSystem::getComponentName() const
{
	static const string name = "Lighting";
	return name;
}
type_index LightingRenderSystem::getComponentType() const
{
	return typeid(LightingRenderComponent);
}
View<Component> LightingRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<LightingRenderComponent>(instance)));
}
void LightingRenderSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
void LightingRenderSystem::setConsts(bool useShadowBuffer, bool useAoBuffer)
{
	if (this->hasShadowBuffer == useShadowBuffer && this->hasAoBuffer == useAoBuffer)
		return;

	this->hasShadowBuffer = useShadowBuffer; this->hasAoBuffer = useAoBuffer;
	if (!lightingPipeline)
		return;

	auto graphicsSystem = GraphicsSystem::getInstance();
	graphicsSystem->destroy(lightingDescriptorSet);
	lightingDescriptorSet = {};

	auto lightingComponents = components.getData();
	auto lightingOccupancy = components.getOccupancy();
	for (uint32 i = 0; i < lightingOccupancy; i++)
	{
		auto lightingView = &lightingComponents[i];
		if (lightingView->descriptorSet.isLastRef())
			graphicsSystem->destroy(ID<DescriptorSet>(lightingView->descriptorSet));
		lightingView->descriptorSet = {};
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
ID<GraphicsPipeline> LightingRenderSystem::getLightingPipeline()
{
	if (!lightingPipeline)
		lightingPipeline = createLightingPipeline(hasShadowBuffer, hasAoBuffer);
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
		createShadowFramebuffers(shadowFramebuffers, getShadowImageViews());
	return shadowFramebuffers;
}
const ID<Framebuffer>* LightingRenderSystem::getAoFramebuffers()
{
	if (!aoFramebuffers[0])
		createAoFramebuffers(aoFramebuffers, getAoImageViews());
	return aoFramebuffers;
}

//**********************************************************************************************************************
ID<Image> LightingRenderSystem::getDfgLUT()
{
	if (!dfgLUT)
		dfgLUT = createDfgLUT();
	return dfgLUT;
}
ID<Image> LightingRenderSystem::getShadowBuffer()
{
	if (!shadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowBuffer;
}
ID<Image> LightingRenderSystem::getAoBuffer()
{
	if (!aoBuffer)
		aoBuffer = createAoBuffer(aoImageViews);
	return aoBuffer;
}
const ID<ImageView>* LightingRenderSystem::getShadowImageViews()
{
	if (!shadowBuffer)
		shadowBuffer = createShadowBuffer(shadowImageViews);
	return shadowImageViews;
}
const ID<ImageView>* LightingRenderSystem::getAoImageViews()
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
	auto invCubemapSize = cubemapSize - 1;
	auto invDim = 1.0f / cubemapSize;
	float shb[shCoefCount];

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

			for (uint32 j = 0; j < shCoefCount; j++)
				sh[j] += color * shb[j];
		}
	}
}

//**********************************************************************************************************************
static ID<Buffer> generateIblSH(GraphicsSystem* graphicsSystem, ThreadSystem* threadSystem,
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

		threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			calcIblSH(shBufferData, faces, cubemapSize, task.getTaskIndex(),
				task.getItemOffset(), task.getItemCount());
		}),
		cubemapSize * cubemapSize);
		threadPool.wait();
	}
	else
	{
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
	return graphicsSystem->createBuffer(Buffer::Bind::TransferDst | Buffer::Bind::Uniform,
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

	countBufferData[taskIndex] = count;
}

//**********************************************************************************************************************
static ID<Image> generateIblSpecular(GraphicsSystem* graphicsSystem, ThreadSystem* threadSystem,
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
	auto countBufferData = countBuffer.data();
	auto specularMap = (SpecularItem*)cpuSpecularCacheView->getMap();

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			calcIblSpecular(specularMap, countBufferData, cubemapSize,
				cubemapMipCount, specularMipCount, task.getTaskIndex());
		}),
		(uint32)countBuffer.size());
		threadPool.wait();
	}
	else
	{
		for (uint32 i = 0; i < (uint32)countBuffer.size(); i++)
		{
			calcIblSpecular(specularMap, countBufferData, cubemapSize,
				cubemapMipCount, specularMipCount, i);
		}
	}

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

//**********************************************************************************************************************
void LightingRenderSystem::loadCubemap(const fs::path& path, Ref<Image>& cubemap,
	Ref<Buffer>& sh, Ref<Image>& specular, Memory::Strategy strategy)
{
	GARDEN_ASSERT(!path.empty());
	
	vector<uint8> left, right, bottom, top, back, front;
	int2 size; Image::Format format;
	ResourceSystem::getInstance()->loadCubemapData(path, left, right, bottom, top, back, front, size, format);
	auto cubemapSize = size.x;

	auto mipCount = calcMipCount(cubemapSize);
	Image::Mips mips(mipCount);
	mips[0] = { right.data(), left.data(), top.data(), bottom.data(), front.data(), back.data() };

	for (uint8 i = 1; i < mipCount; i++)
		mips[i] = Image::Layers(6);

	auto graphicsSystem = GraphicsSystem::getInstance();
	graphicsSystem->startRecording(CommandBufferType::Graphics);

	cubemap = Ref<Image>(graphicsSystem->createImage(Image::Type::Cubemap,
		Image::Format::SfloatR16G16B16A16, Image::Bind::TransferDst | Image::Bind::TransferSrc |
		Image::Bind::Sampled, mips, int3(size, 1), strategy, format));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, cubemap, "image.cubemap." + path.generic_string());

	auto cubemapView = graphicsSystem->get(cubemap);
	cubemapView->generateMips();

	auto threadSystem = Manager::getInstance()->get<ThreadSystem>();
	sh = Ref<Buffer>(generateIblSH(graphicsSystem, threadSystem, mips[0], cubemapSize, strategy));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, sh, "buffer.sh." + path.generic_string());

	specular = Ref<Image>(generateIblSpecular(graphicsSystem, threadSystem, 
		iblSpecularPipeline, ID<Image>(cubemap), strategy));
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, specular, "image.cubemap.specular." + path.generic_string());
	
	graphicsSystem->stopRecording();
}

//**********************************************************************************************************************
Ref<DescriptorSet> LightingRenderSystem::createDescriptorSet(ID<Buffer> sh, ID<Image> specular)
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto specularView = graphicsSystem->get(specular);

	map<string, DescriptorSet::Uniform> iblUniforms =
	{ 
		{ "data", DescriptorSet::Uniform(sh) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
	};

	auto descritptorSet = graphicsSystem->createDescriptorSet(lightingPipeline, std::move(iblUniforms), 1);
	return Ref<DescriptorSet>(descritptorSet);
}
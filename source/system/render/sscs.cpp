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

#include "garden/system/render/sscs.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/settings.hpp"
#include "garden/profiler.hpp"

#define WAVE_SIZE 64

using namespace garden;

namespace garden
{
	struct DispatchData final
	{
		int3 waveCount = int3::zero;
		int2 waveOffset = int2::zero;
	};
	struct DispatchList final
	{
		float4 lightCoordinate = float4::zero;
		DispatchData dispatches[8];
		uint32 dispatchCount = 0;
	};
}

//**********************************************************************************************************************
static ID<ComputePipeline> createPipeline(const SscsRenderSystem::Properties& properties)
{
	map<string, Pipeline::SpecConstValue> specConsts =
	{
		{ "HARD_SHADOW_SAMPLES", Pipeline::SpecConstValue(properties.hardShadowSamples) },
		{ "FADE_OUT_SAMPLES", Pipeline::SpecConstValue(properties.fadeOutSamples) },
		{ "SURFACE_THICKNESS", Pipeline::SpecConstValue(properties.surfaceThickness) },
		{ "BILINEAR_THRESHOLD", Pipeline::SpecConstValue(properties.bilinearThreshold) },
		{ "SHADOW_CONTRAST", Pipeline::SpecConstValue(properties.shadowContrast) },
		{ "IGNORE_EDGE_PIXELS", Pipeline::SpecConstValue(properties.ignoreEdgePixels) },
		{ "USE_PRECISION_OFFSET", Pipeline::SpecConstValue(properties.usePrecisionOffset) },
		{ "BILINEAR_SAMPLING_OFFSET_MODE", Pipeline::SpecConstValue(properties.bilinearSamplingOffsetMode) },
		{ "DEBUG_OUTPUT_EDGE_MASK", Pipeline::SpecConstValue(properties.debugOutputEdgeMask) },
		{ "DEBUG_OUTPUT_THREAD_INDEX", Pipeline::SpecConstValue(properties.debugOutputThreadIndex) },
		{ "DEBUG_OUTPUT_WAVE_INDEX", Pipeline::SpecConstValue(properties.debugOutputWaveIndex) },
		{ "NEAR_DEPTH_VALUE", Pipeline::SpecConstValue(1.0f) },
		{ "FAR_DEPTH_VALUE", Pipeline::SpecConstValue(0.0f) },
		{ "DEPTH_BOUNDS_X", Pipeline::SpecConstValue(properties.depthBounds.x) },
		{ "DEPTH_BOUNDS_Y", Pipeline::SpecConstValue(properties.depthBounds.y) },
		{ "USE_EARLY_OUT", Pipeline::SpecConstValue(properties.useEarlyOut) }
	};
	return ResourceSystem::Instance::get()->loadComputePipeline("sscs", false, true, 0, specConsts);
}
static map<string, DescriptorSet::Uniform> getUniforms()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
	GARDEN_ASSERT(pbrLightingSystem->useShadowBuffer());
	auto gFramebufferView = GraphicsSystem::Instance::get()->get(deferredSystem->getGFramebuffer());
	auto depthStencilAttachment = gFramebufferView->getDepthStencilAttachment();

	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "depthBuffer", DescriptorSet::Uniform(depthStencilAttachment.imageView) },
		{ "shadowBuffer", DescriptorSet::Uniform(pbrLightingSystem->getShadowImageViews()[0]) },
	};
	return uniforms;
}

//**********************************************************************************************************************
SscsRenderSystem::SscsRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SscsRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SscsRenderSystem::deinit);
}
SscsRenderSystem::~SscsRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SscsRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SscsRenderSystem::deinit);
	}

	unsetSingleton();
}

void SscsRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PostShadowRender", SscsRenderSystem::postShadowRender);
	ECSM_SUBSCRIBE_TO_EVENT("ShadowRecreate", SscsRenderSystem::shadowRecreate);

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
		settingsSystem->getBool("sscs.isEnabled", isEnabled);

	auto pbrLightingSystem = PbrLightingRenderSystem::Instance::get();
	if (isEnabled && pbrLightingSystem->useShadowBuffer())
	{
		if (!pipeline)
			pipeline = createPipeline(properties);
	}
}
void SscsRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		graphicsSystem->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PostShadowRender", SscsRenderSystem::postShadowRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("ShadowRecreate", SscsRenderSystem::shadowRecreate);
	}
}

//**********************************************************************************************************************
static constexpr int32 bendMin(int32 a, int32 b) noexcept { return a > b ? b : a; }
static constexpr int32 bendMax(int32 a, int32 b) noexcept { return a > b ? a : b; }

static DispatchList buildDispatchList(float4 lightProjection, int4 renderBounds, 
	int32 waveSize = WAVE_SIZE, bool expandedZRange = false) noexcept
{
	DispatchList result = {};

	auto xyLightW = lightProjection.w;
	auto fpLimit = 0.000002f * waveSize;

	if (xyLightW >= 0 && xyLightW < fpLimit)
		xyLightW = fpLimit;
	else if (xyLightW < 0 && xyLightW > -fpLimit)
		xyLightW = -fpLimit;

	result.lightCoordinate = float4(
		((lightProjection.x / xyLightW) *  0.5f + 0.5f) * renderBounds.z,
		((lightProjection.y / xyLightW) * -0.5f + 0.5f) * renderBounds.w,
		(lightProjection.w == 0.0f) ? 0.0f : lightProjection.z / lightProjection.w,
		(lightProjection.w > 0.0f) ? 1.0f : -1.0f);

	if (expandedZRange)
		result.lightCoordinate.z = result.lightCoordinate.z * 0.5f + 0.5f;

	auto lightXY = int2((float2)result.lightCoordinate + 0.5f);
	auto biasedBounds = int4(
		renderBounds.x - lightXY.x, -(renderBounds.w - lightXY.y),
		renderBounds.z - lightXY.x, -(renderBounds.y - lightXY.y));

	for (int q = 0; q < 4; q++)
	{
		auto vertical = q == 0 || q == 3;
		auto bounds = int4(
			bendMax(0, ((q & 1) ? biasedBounds.x : -biasedBounds.z)),
			bendMax(0, ((q & 2) ? biasedBounds.y : -biasedBounds.w)),
			bendMax(0, (((q & 1) ? biasedBounds.z : -biasedBounds.x) + waveSize * (vertical ? 1 : 2) - 1)),
			bendMax(0, (((q & 2) ? biasedBounds.w : -biasedBounds.y) + waveSize * (vertical ? 2 : 1) - 1))) / waveSize;

		if ((bounds.z - bounds.x) > 0 && (bounds.w - bounds.y) > 0)
		{
			auto biasX = (q == 2 || q == 3) ? 1 : 0;
			auto biasY = (q == 1 || q == 3) ? 1 : 0;

			auto& dispatch = result.dispatches[result.dispatchCount++];
			dispatch.waveCount = int3(waveSize, bounds.z - bounds.x, bounds.w - bounds.y);
			dispatch.waveOffset = int2(
				((q & 1) ?  bounds.x : -bounds.z) + biasX, 
				((q & 2) ? -bounds.w :  bounds.y) + biasY);

			auto axisDelta = biasedBounds.x - biasedBounds.y;
			if (q == 1) axisDelta =  biasedBounds.z + biasedBounds.y;
			if (q == 2) axisDelta = -biasedBounds.x - biasedBounds.w;
			if (q == 3) axisDelta = -biasedBounds.z + biasedBounds.w;
			axisDelta = (axisDelta + waveSize - 1) / waveSize;

			if (axisDelta > 0)
			{
				auto& dispatch2 = result.dispatches[result.dispatchCount++];
				dispatch2 = dispatch;

				if (q == 0)
				{
					dispatch2.waveCount.z = bendMin(dispatch.waveCount.z, axisDelta);
					dispatch.waveCount.z -= dispatch2.waveCount.z;
					dispatch2.waveOffset.y = dispatch.waveOffset.y + dispatch.waveCount.z;
					dispatch2.waveOffset.x--;
					dispatch2.waveCount.y++;
				}
				if (q == 1)
				{
					dispatch2.waveCount.y = bendMin(dispatch.waveCount.y, axisDelta);
					dispatch.waveCount.y -= dispatch2.waveCount.y;
					dispatch2.waveOffset.x = dispatch.waveOffset.x + dispatch.waveCount.y;
					dispatch2.waveCount.z++;
				}
				if (q == 2)
				{
					dispatch2.waveCount.y = bendMin(dispatch.waveCount.y, axisDelta);
					dispatch.waveCount.y -= dispatch2.waveCount.y;
					dispatch.waveOffset.x += dispatch2.waveCount.y;
					dispatch2.waveCount.z++;
					dispatch2.waveOffset.y--;
				}
				if (q == 3)
				{
					dispatch2.waveCount.z = bendMin(dispatch.waveCount.z, axisDelta);
					dispatch.waveCount.z -= dispatch2.waveCount.z;
					dispatch.waveOffset.y += dispatch2.waveCount.z;
					dispatch2.waveCount.y++;
				}

				if (dispatch2.waveCount.y <= 0 || dispatch2.waveCount.z <= 0)
					dispatch2 = result.dispatches[--result.dispatchCount];
				if (dispatch.waveCount.y <= 0 || dispatch.waveCount.z <= 0)
					dispatch = result.dispatches[--result.dispatchCount];
			}
		}
	}

	for (uint32 i = 0; i < result.dispatchCount; i++)
		result.dispatches[i].waveOffset *= waveSize;
	return result;
}

//**********************************************************************************************************************
void SscsRenderSystem::postShadowRender()
{
	SET_CPU_ZONE_SCOPED("SSCS Post Shadow Render");

	if (!isEnabled)
		return;

	if (!pipeline)
		pipeline = createPipeline(properties);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return;
	
	if (!descriptorSet)
	{
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.sscs");
	}

	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto lightProjection = (float4)(cameraConstants.viewProj * f32x4(-cameraConstants.lightDir, 0.0f));
	auto viewportSize = (int2)graphicsSystem->getScaledFramebufferSize();
	auto dispatchList = buildDispatchList(lightProjection, int4(int2::zero, viewportSize));
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->lightCoordinate = dispatchList.lightCoordinate;
	pushConstants->invDepthTexSize = 1.0f / (float2)viewportSize;

	SET_GPU_DEBUG_LABEL("SSCS", Color::transparent);
	pipelineView->bind();
	pipelineView->bindDescriptorSet(descriptorSet);

	for (uint32 i = 0; i < dispatchList.dispatchCount; i++)
	{
		const auto& dispatch = dispatchList.dispatches[i];
		pushConstants->waveOffset = dispatch.waveOffset;
		pipelineView->pushConstants();
		pipelineView->dispatch(u32x4((uint3)dispatch.waveCount), false);
	}
	
	PbrLightingRenderSystem::Instance::get()->markAnyShadow();
}

void SscsRenderSystem::shadowRecreate()
{
	if (descriptorSet)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(descriptorSet);
		auto uniforms = getUniforms();
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.sscs");
	}
}

//**********************************************************************************************************************
ID<ComputePipeline> SscsRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(properties);
	return pipeline;
}
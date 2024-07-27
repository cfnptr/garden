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
#include "garden/system/render/shadow-mapping.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/shadow-mapping.hpp"
#endif

#include <cfloat>

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4 farNearPlanes;
		float4 lightDir;
		float minBias;
		float maxBias;
		float intensity;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<Image> createShadowData(GraphicsSystem* graphicsSystem,
	vector<ID<ImageView>>& imageViews, int32 shadowMapSize)
{
	const auto shadowFormat = Image::Format::UnormD16;
	auto image = graphicsSystem->createImage(shadowFormat, Image::Bind::DepthStencilAttachment |
		Image::Bind::Sampled, { Image::Layers(SHADOW_MAP_CASCADE_COUNT) },
		int2(shadowMapSize), Image::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.shadow-mapping.buffer");
	imageViews.resize(SHADOW_MAP_CASCADE_COUNT);

	for (uint8 i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
	{
		auto imageView = graphicsSystem->createImageView(image,
			Image::Type::Texture2D, shadowFormat, 0, 1, i, 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, imageView,
			"imageView.shadow-mapping.buffer" + to_string(i));
		imageViews[i] = imageView;
	}
	
	return image;
}

//--------------------------------------------------------------------------------------------------
static void createDataBuffers(GraphicsSystem* graphicsSystem,
	vector<vector<ID<Buffer>>>& dataBuffers)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	dataBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Uniform,
			Buffer::Access::SequentialWrite, sizeof(ShadowMappingRenderSystem::DataBuffer),
			Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer,
			"buffer.uniform.shadow-mapping.data" + to_string(i));
		dataBuffers[i].push_back(buffer);
	}
}

//--------------------------------------------------------------------------------------------------
static void createFramebuffers(
	GraphicsSystem* graphicsSystem, const vector<ID<ImageView>>& imageViews,
	vector<ID<Framebuffer>>& framebuffers, int32 shadowMapSize)
{
	framebuffers.resize(SHADOW_MAP_CASCADE_COUNT);
	Framebuffer::OutputAttachment depthStencilAttachment({}, true, false, true);

	for (uint8 i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
	{
		vector<Framebuffer::OutputAttachment> colorAttachments;
		depthStencilAttachment.imageView = imageViews[i];
		auto framebuffer = graphicsSystem->createFramebuffer(
			int2(shadowMapSize), std::move(colorAttachments), depthStencilAttachment);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, framebuffer,
			"framebuffer.shadow" + to_string(i));
		framebuffers[i] = framebuffer;
	}
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline()
{
	auto lightingSystem = manager->get<LightingRenderSystem>();
	lightingSystem->setConsts(true, lightingSystem->getUseAoBuffer());
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"shadow/mapping", lightingSystem->getShadowFramebuffers()[0]);
}

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getUniforms(
	ID<Image> shadowMap, const vector<vector<ID<Buffer>>>& dataBuffers)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	auto shadowMapView = graphicsSystem->get(shadowMap);
	auto gFramebuffer = graphicsSystem->get(DeferredRenderSystem::getInstance()->getGFramebuffer());
	const auto& colorAttachments = gFramebuffer->getColorAttachments();
	auto depthStencilAttachment = gFramebuffer->getDepthStencilAttachment();
	
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "gBuffer1", DescriptorSet::Uniform(
			colorAttachments[1].imageView, 1, swapchainSize) },
		{ "depthBuffer", DescriptorSet::Uniform(
			depthStencilAttachment.imageView, 1, swapchainSize) },
		{ "shadowMap", DescriptorSet::Uniform(
			shadowMapView->getDefaultView(), 1, swapchainSize) },
		{ "data", DescriptorSet::Uniform(dataBuffers) }
	};
	return uniforms;
}

//--------------------------------------------------------------------------------------------------
void ShadowMappingRenderSystem::initialize()
{
	auto graphicsSystem = getGraphicsSystem();
	auto settingsSystem = getManager()->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getInt("shadowMapSize", shadowMapSize);

	if (!pipeline)
		pipeline = createPipeline(getManager());
	if (!shadowMap)
		shadowMap = createShadowData(graphicsSystem, imageViews, shadowMapSize);
	if (dataBuffers.empty())
		createDataBuffers(graphicsSystem, dataBuffers);
	if (framebuffers.empty())
		createFramebuffers(graphicsSystem, imageViews, framebuffers, shadowMapSize);
	
	#if GARDEN_EDITOR
	editor = new ShadowMappingEditor(this);
	#endif
}
void ShadowMappingRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (ShadowMappingEditor*)editor;
	#endif
}
void ShadowMappingRenderSystem::render()
{
	#if GARDEN_EDITOR
	((ShadowMappingEditor*)editor)->render();
	#endif
}

//--------------------------------------------------------------------------------------------------
uint32 ShadowMappingRenderSystem::getShadowPassCount()
{
	return SHADOW_MAP_CASCADE_COUNT;
}

//--------------------------------------------------------------------------------------------------
static float4x4 calcLightViewProj(
	float fieldOfView, float aspectRatio, float nearPlane, float farPlane, float zCoeff,
	const float4x4& view, const float3& lightDir, float3& cameraOffset)
{
	auto proj = calcPerspProjRevZ(fieldOfView, aspectRatio, nearPlane, farPlane);
	auto viewProjInv = inverse(proj * view);

	uint8 cornerIndex = 0;
	float4 frustumCorners[8];

	for (uint8 z = 0; z < 2; z++)
	{
		for (uint8 y = 0; y < 2; y++)
		{
			for (uint8 x = 0; x < 2; x++)
			{
				auto corner = viewProjInv * float4(
					x * 2.0f - 1.0f, y * 2.0f - 1.0f, z, 1.0f);
				frustumCorners[cornerIndex++] = corner / corner.w;
			}
		}
	}

	auto center = float3(0.0f);
	for (uint8 i = 0; i < 8; i++)
		center += (float3)frustumCorners[i];
	center *= (1.0f / 8.0f);

	auto lightView = lookAt(center, center + lightDir);
	auto minimum = float3(FLT_MAX), maximum = float3(-FLT_MAX);

	for (uint8 i = 0; i < 8; i++)
	{
		auto trf = lightView * frustumCorners[i];
		minimum = min(minimum, (float3)trf);
		maximum = max(maximum, (float3)trf);
	}

	if (minimum.z < 0.0f)
		minimum.z *= zCoeff;
	else
		minimum.z /= zCoeff;

	if (maximum.z < 0.0f)
		maximum.z /= zCoeff;
	else
		maximum.z *= zCoeff;
		
	cameraOffset = lightDir * minimum.z + center;

	auto lightProj = calcOrthoProjRevZ(
		float2(minimum.x, maximum.x),
		float2(minimum.y, maximum.y),
		float2(minimum.z, maximum.z));
	return lightProj * lightView;
}

//--------------------------------------------------------------------------------------------------
bool ShadowMappingRenderSystem::prepareShadowRender(uint32 passIndex,
	float4x4& viewProj, float3& cameraOffset, ID<Framebuffer>& framebuffer)
{
	auto graphicsSystem = getGraphicsSystem();
	auto camera = graphicsSystem->camera;
	auto directionalLight = graphicsSystem->directionalLight;
	if (!camera || !directionalLight)
		return false;

	auto cameraView = getManager()->get<CameraComponent>(camera);
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	
	auto nearPlane = cameraView->p.perspective.nearPlane;
	auto farPlane = nearPlane + this->farPlane * splitCoefs[passIndex];
	if (passIndex > 0)
		nearPlane += this->farPlane * splitCoefs[passIndex - 1];
	farPlanes[passIndex] = farPlane;

	viewProj = calcLightViewProj(cameraView->p.perspective.fieldOfView,
		cameraView->p.perspective.aspectRatio, nearPlane, farPlane, zCoeff,
		cameraConstants.view, (float3)cameraConstants.lightDir, cameraOffset);

	// TODO: Check if the camera offset is correct.
	// And shadow objects are rendered in the correct order.

	const float4x4 ndcToCoords
	(
		0.5f, 0.0f, 0.0f, 0.5f,
		0.0f, 0.5f, 0.0f, 0.5f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);
	const float4x4 coordsToNDC
	(
		2.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 2.0f, 0.0f, -1.0f,
		0.0f, 0.0f, 1.0f,  0.0f,
		0.0f, 0.0f, 0.0f,  1.0f
	);

	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	auto dataBufferView = graphicsSystem->get(dataBuffers[swapchainIndex][0]);
	auto data = (DataBuffer*)dataBufferView->getMap();
	data->lightSpace[passIndex] = ndcToCoords * viewProj *
		cameraConstants.viewProjInv * coordsToNDC;
	framebuffer = framebuffers[passIndex];
	return true;
}
void ShadowMappingRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if ((changes.framebufferSize || changes.bufferCount) && descriptorSet)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto uniforms = getUniforms(getManager(),
			graphicsSystem, shadowMap, dataBuffers);
		auto descriptorSetView = graphicsSystem->get(descriptorSet);
		descriptorSetView->recreate(std::move(uniforms));
	}
}

//--------------------------------------------------------------------------------------------------
void ShadowMappingRenderSystem::beginShadowRender(
	uint32 passIndex, MeshRenderType renderType)
{
	if (renderType != MeshRenderType::OpaqueShadow)
		return;
	auto framebufferView = getGraphicsSystem()->get(framebuffers[passIndex]);
	framebufferView->beginRenderPass(nullptr, 0, 0.0f, 0, int4(0), isAsync);
}
void ShadowMappingRenderSystem::endShadowRender(
	uint32 passIndex, MeshRenderType renderType)
{
	if (renderType != MeshRenderType::OpaqueShadow)
		return;
	auto framebufferView = getGraphicsSystem()->get(framebuffers[passIndex]);
	framebufferView->endRenderPass();
}

//--------------------------------------------------------------------------------------------------
bool ShadowMappingRenderSystem::shadowRender()
{
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady())
		return false;

	if (!descriptorSet)
	{
		auto uniforms = getUniforms(getManager(),
			graphicsSystem, shadowMap, dataBuffers);
		descriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
			"descriptorSet.shadow-mapping");
	}

	auto swapchainIndex = graphicsSystem->getSwapchainIndex();
	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto dataBufferView = graphicsSystem->get(dataBuffers[swapchainIndex][0]);
	dataBufferView->flush();

	SET_GPU_DEBUG_LABEL("Cascade Shadow Mapping", Color::transparent);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, swapchainIndex);
	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->farNearPlanes = float4(
		cameraConstants.nearPlane / farPlanes, cameraConstants.nearPlane);
	pushConstants->lightDir = cameraConstants.lightDir;
	pushConstants->minBias = minBias;
	pushConstants->maxBias = maxBias;
	pushConstants->intensity = intensity;
	pipelineView->pushConstants();
	pipelineView->drawFullscreen();
	return true;
}

//--------------------------------------------------------------------------------------------------
ID<Image> ShadowMappingRenderSystem::getShadowMap()
{
	if (!shadowMap)
		shadowMap = createShadowData(getGraphicsSystem(), imageViews, shadowMapSize);
	return shadowMap;
}
ID<GraphicsPipeline> ShadowMappingRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline(getManager());
	return pipeline;
}
const vector<vector<ID<Buffer>>>& ShadowMappingRenderSystem::getDataBuffers()
{
	if (dataBuffers.empty())
		createDataBuffers(getGraphicsSystem(), dataBuffers);
	return dataBuffers;
}
const vector<ID<Framebuffer>>& ShadowMappingRenderSystem::getFramebuffers()
{
	if (framebuffers.empty())
		createFramebuffers(getGraphicsSystem(), imageViews, framebuffers, shadowMapSize);
	return framebuffers;
}

//--------------------------------------------------------------------------------------------------
void ShadowMappingRenderSystem::setShadowMapSize(int32 size)
{
	abort(); // TODO:
}
*/
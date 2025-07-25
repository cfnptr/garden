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

#include "garden/system/render/skybox.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/resource/primitive.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/profiler.hpp"

using namespace garden;
using namespace garden::primitive;

static ID<GraphicsPipeline> createPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = deferredSystem->useAsyncRecording();

	auto skyboxPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"skybox", deferredSystem->getDepthHdrFramebuffer(), options);
	return skyboxPipeline;
}

SkyboxRenderSystem::SkyboxRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SkyboxRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SkyboxRenderSystem::deinit);
}
SkyboxRenderSystem::~SkyboxRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SkyboxRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SkyboxRenderSystem::deinit);
	}

	unsetSingleton();
}

void SkyboxRenderSystem::resetComponent(View<Component> component, bool full)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto skyboxView = View<SkyboxRenderComponent>(component);
	resourceSystem->destroyShared(skyboxView->cubemap);
	resourceSystem->destroyShared(skyboxView->descriptorSet);
	skyboxView->cubemap = {}; skyboxView->descriptorSet = {}; 
}
void SkyboxRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<SkyboxRenderComponent>(destination);
	const auto sourceView = View<SkyboxRenderComponent>(source);
	destinationView->cubemap = sourceView->cubemap;
	destinationView->descriptorSet = sourceView->descriptorSet;
}
string_view SkyboxRenderSystem::getComponentName() const
{
	return "Skybox";
}

//**********************************************************************************************************************
void SkyboxRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", SkyboxRenderSystem::imageLoaded);
	ECSM_SUBSCRIBE_TO_EVENT("DepthHdrRender", SkyboxRenderSystem::depthHdrRender);

	if (!pipeline)
		pipeline = createPipeline();
}
void SkyboxRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsSystem::Instance::get()->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SkyboxRenderSystem::imageLoaded);
		ECSM_UNSUBSCRIBE_FROM_EVENT("DepthHdrRender", SkyboxRenderSystem::depthHdrRender);
	}
}

void SkyboxRenderSystem::imageLoaded()
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto image = resourceSystem->getLoadedImage();
	auto& imagePath = resourceSystem->getLoadedImagePaths()[0];
	Ref<DescriptorSet> descriptorSet = {};

	for (auto& skybox : components)
	{
		if (skybox.cubemap != image || skybox.descriptorSet)
			continue;
		if (!descriptorSet)
			descriptorSet = createSharedDS(imagePath.generic_string(), image);
		skybox.descriptorSet = descriptorSet;
	}
}

//**********************************************************************************************************************
void SkyboxRenderSystem::depthHdrRender()
{
	SET_CPU_ZONE_SCOPED("Skybox Depth HDR Render");

	if (!isEnabled)
		return;
		
	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!graphicsSystem->camera)
		return;

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(graphicsSystem->camera);
	if (transformView && !transformView->isActive())
		return;
	
	auto skyboxView = tryGetComponent(graphicsSystem->camera);
	if (!skyboxView || !skyboxView->cubemap || !skyboxView->descriptorSet)
		return;

	auto pipelineView = graphicsSystem->get(pipeline);
	auto cubemapView = graphicsSystem->get(skyboxView->cubemap);
	if (!pipelineView->isReady() || !cubemapView->isReady())
		return;

	const auto& cameraConstants = graphicsSystem->getCameraConstants();

	PushConstants pc;
	pc.viewProj = (float4x4)cameraConstants.viewProj;

	SET_GPU_DEBUG_LABEL("Skybox", Color::transparent);
	if (graphicsSystem->isCurrentRenderPassAsync())
	{
		pipelineView->bindAsync(0, 0);
		pipelineView->setViewportScissorAsync(float4::zero, 0);
		pipelineView->bindDescriptorSetAsync(ID<DescriptorSet>(skyboxView->descriptorSet), 0, 0);
		pipelineView->pushConstantsAsync(&pc, 0);
		pipelineView->drawAsync(0, {}, primitive::cubeVertices.size());
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(ID<DescriptorSet>(skyboxView->descriptorSet));
		pipelineView->pushConstants(&pc);
		pipelineView->draw({}, primitive::cubeVertices.size());
	}
}

//**********************************************************************************************************************
ID<GraphicsPipeline> SkyboxRenderSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}

Ref<DescriptorSet> SkyboxRenderSystem::createSharedDS(string_view path, ID<Image> cubemap)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(cubemap);

	auto cubemapView = GraphicsSystem::Instance::get()->get(cubemap);
	auto imageSize = (uint2)cubemapView->getSize();
	auto imageType = cubemapView->getType();

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.data(), path.length());
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));
	Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

	DescriptorSet::Uniforms uniforms =
	{ { "cubemap", DescriptorSet::Uniform(cubemapView->getDefaultView()) } };
	auto descriptorSet = ResourceSystem::Instance::get()->createSharedDS(
		Hash128::digestState(hashState), pipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + string(path));
	return descriptorSet;
}
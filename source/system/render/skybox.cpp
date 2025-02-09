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

bool SkyboxRenderComponent::destroy()
{
	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(descriptorSet);
	resourceSystem->destroyShared(cubemap);
	cubemap = {}; descriptorSet = {};
	return true;
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
	else
	{
		components.clear(false);
	}

	unsetSingleton();
}

const string& SkyboxRenderSystem::getComponentName() const
{
	static const string name = "Skybox";
	return name;
}

//**********************************************************************************************************************
static ID<GraphicsPipeline> createPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();
	auto skyboxPipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"skybox", deferredSystem->getMetaHdrFramebuffer(), deferredSystem->useAsyncRecording());
	return skyboxPipeline;
}

void SkyboxRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", SkyboxRenderSystem::imageLoaded);
	ECSM_SUBSCRIBE_TO_EVENT("MetaHdrRender", SkyboxRenderSystem::metaHdrRender);

	if (!pipeline)
		pipeline = createPipeline();
}
void SkyboxRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsSystem::Instance::get()->destroy(pipeline);

		ECSM_UNSUBSCRIBE_FROM_EVENT("ImageLoaded", SkyboxRenderSystem::imageLoaded);
		ECSM_UNSUBSCRIBE_FROM_EVENT("MetaHdrRender", SkyboxRenderSystem::metaHdrRender);
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
void SkyboxRenderSystem::metaHdrRender()
{
	SET_CPU_ZONE_SCOPED("Skybox Meta HDR Render");

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

	const auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto pushConstants = pipelineView->getPushConstants<PushConstants>(0);
	pushConstants->viewProj = cameraConstants.viewProj;

	SET_GPU_DEBUG_LABEL("Skybox", Color::transparent);
	if (graphicsSystem->isCurrentRenderPassAsync())
	{
		pipelineView->bindAsync(0, 0);
		pipelineView->setViewportScissorAsync(float4(0.0f), 0);
		pipelineView->bindDescriptorSetAsync(ID<DescriptorSet>(skyboxView->descriptorSet), 0, 0);
		pipelineView->pushConstantsAsync(0);
		pipelineView->drawAsync(0, {}, primitive::cubeVertices.size());
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(ID<DescriptorSet>(skyboxView->descriptorSet));
		pipelineView->pushConstants();
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

Ref<DescriptorSet> SkyboxRenderSystem::createSharedDS(const string& path, ID<Image> cubemap)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(cubemap);

	auto cubemapView = GraphicsSystem::Instance::get()->get(cubemap);
	auto imageSize = (uint2)cubemapView->getSize();
	auto imageType = cubemapView->getType();

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.c_str(), path.length());
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));
	Hash128::updateState(hashState, &imageType, sizeof(Image::Type));

	map<string, DescriptorSet::Uniform> uniforms =
	{ { "cubemap", DescriptorSet::Uniform(cubemapView->getDefaultView()) } };
	auto descriptorSet = ResourceSystem::Instance::get()->createSharedDS(
		Hash128::digestState(hashState), pipeline, std::move(uniforms));
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + path);
	return descriptorSet;
}
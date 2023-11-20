//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/graphics/skybox.hpp"
#include "garden/system/graphics/editor/skybox.hpp"
#include "garden/system/graphics/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4x4 viewProj;
	};
}

//--------------------------------------------------------------------------------------------------
static ID<GraphicsPipeline> createPipeline(DeferredRenderSystem* deferredSystem)
{
	auto skyboxPipeline = ResourceSystem::getInstance()->loadGraphicsPipeline(
		"skybox", deferredSystem->getHdrFramebuffer(), deferredSystem->isRenderAsync());
	return skyboxPipeline;
}

//--------------------------------------------------------------------------------------------------
void SkyboxRenderSystem::initialize()
{
	auto graphicsSystem = getGraphicsSystem();	
	fullCubeVertices = graphicsSystem->getFullCubeVertices();
	if (!pipeline) pipeline = createPipeline(getDeferredSystem());

	#if GARDEN_EDITOR
	editor = new SkyboxEditor(this);
	#endif
}
void SkyboxRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (SkyboxEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void SkyboxRenderSystem::hdrRender()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	auto camera = graphicsSystem->camera;
	auto pipelineView = graphicsSystem->get(pipeline);
	auto fullCubeView = graphicsSystem->get(fullCubeVertices);
	if (!camera || !pipelineView->isReady() || !fullCubeView->isReady()) return;
	
	auto component = manager->tryGet<SkyboxRenderComponent>(graphicsSystem->camera);
	if (!component) return;
	auto cubemapView = graphicsSystem->get(component->cubemap);
	if (!cubemapView->isReady()) return;

	if (!component->descriptorSet)
	{
		auto descriptorSet = createDescriptorSet(component->cubemap);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
			"descriptorSet.skybox" + to_string(*descriptorSet));
		component->descriptorSet = descriptorSet;
	}

	auto deferredSystem = getDeferredSystem();
	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();

	SET_GPU_DEBUG_LABEL("Skybox", Color::transparent);
	if (deferredSystem->isRenderAsync())
	{
		pipelineView->bindAsync(0, 0);
		pipelineView->setViewportScissorAsync(float4(float2(0),
			getDeferredSystem()->getFramebufferSize()), 0);
		pipelineView->bindDescriptorSetAsync(component->descriptorSet, 0, 0);
		auto pushConstants = pipelineView->getPushConstantsAsync<PushConstants>(0);
		pushConstants->viewProj = cameraConstants.viewProj;
		pipelineView->pushConstantsAsync(0);
		pipelineView->drawAsync(0, fullCubeVertices, 36);
	}
	else
	{
		pipelineView->bind();
		pipelineView->setViewportScissor(float4(float2(0),
			getDeferredSystem()->getFramebufferSize()));
		pipelineView->bindDescriptorSet(component->descriptorSet);
		auto pushConstants = pipelineView->getPushConstants<PushConstants>();
		pushConstants->viewProj = cameraConstants.viewProj;
		pipelineView->pushConstants();
		pipelineView->draw(fullCubeVertices, 36);
	}
}

//--------------------------------------------------------------------------------------------------
type_index SkyboxRenderSystem::getComponentType() const
{
	return typeid(SkyboxRenderComponent);
}
ID<Component> SkyboxRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<CameraComponent>(entity));
	return ID<Component>(components.create());
}
void SkyboxRenderSystem::destroyComponent(ID<Component> instance)
{
	auto graphicsSystem = getGraphicsSystem();
	auto component = components.get(ID<SkyboxRenderComponent>(instance));
	if (component->cubemap.getRefCount() == 1)
		graphicsSystem->destroy(component->cubemap);
	if (component->descriptorSet.getRefCount() == 1)
		graphicsSystem->destroy(component->descriptorSet);
	components.destroy(ID<SkyboxRenderComponent>(instance));
}
View<Component> SkyboxRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<SkyboxRenderComponent>(instance)));
}
void SkyboxRenderSystem::disposeComponents() { components.dispose(); }

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> SkyboxRenderSystem::getPipeline()
{
	auto deferredSystem = getManager()->get<DeferredRenderSystem>();
	if (!pipeline) pipeline = createPipeline(deferredSystem);
	return pipeline;
}

//--------------------------------------------------------------------------------------------------
ID<DescriptorSet> SkyboxRenderSystem::createDescriptorSet(ID<Image> cubemap)
{
	GARDEN_ASSERT(cubemap);
	auto graphicsSystem = getGraphicsSystem();
	auto cubemapView = graphicsSystem->get(cubemap);
	map<string, DescriptorSet::Uniform> uniforms =
	{ { "cubemap", DescriptorSet::Uniform(cubemapView->getDefaultView()) } };
	return graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
}
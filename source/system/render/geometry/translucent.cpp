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

// TODO: refactor this code.

/*
#include "garden/system/render/geometry/translucent.hpp"
#include "garden/system/render/lighting.hpp"
#include "garden/system/render/editor.hpp"

using namespace garden;

#if GARDEN_EDITOR
//--------------------------------------------------------------------------------------------------
static void onTranslucentEntityInspector(ID<Entity> entity, GeometryEditor* editor)
{
	if (ImGui::CollapsingHeader("Translucent Render"))
	{
		auto geometryComponent = manager->get<TranslucentRenderComponent>(entity);
		editor->renderInfo(*geometryComponent, nullptr);
	}
}
#endif

//--------------------------------------------------------------------------------------------------
static map<string, DescriptorSet::Uniform> getLightingUniforms(ID<Buffer> sh, ID<Image> specular)
{
	auto specularView = GraphicsSystem::get()->get(specular);
	map<string, DescriptorSet::Uniform> uniforms =
	{ 
		{ "data", DescriptorSet::Uniform(sh) },
		{ "specular", DescriptorSet::Uniform(specularView->getDefaultView()) }
	};
	return uniforms;
}

void TranslucentRenderSystem::initialize()
{
	GeometryRenderSystem::initialize();

	#if GARDEN_EDITOR
	EditorRenderSystem::getInstance()->registerEntityInspector(typeid(TranslucentRenderComponent),
	[this](ID<Entity> entity)
	{
		onTranslucentEntityInspector(getManager(),
			entity, (GeometryEditor*)editor);
	});
	#endif
}
// TODO: unregister inspector
bool TranslucentRenderSystem::isDrawReady()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	if (!graphicsSystem->camera)
		return false;

	auto lightingComponent = manager->tryGet<LightingRenderComponent>(graphicsSystem->camera);
	if (!lightingComponent || !lightingComponent->cubemap ||
		!lightingComponent->sh || !lightingComponent->specular)
	{
		return false;
	}
	
	auto cubemapView = graphicsSystem->get(lightingComponent->cubemap);
	auto shView = graphicsSystem->get(lightingComponent->sh);
	auto specularView = graphicsSystem->get(lightingComponent->specular);
	if (!cubemapView->isReady() || !shView->isReady() || !specularView->isReady())
		return false;

	if (!lightingDescriptorSet || lightingCubemap != lightingComponent->cubemap)
	{
		graphicsSystem->destroy(lightingDescriptorSet);
		auto uniforms = getLightingUniforms(graphicsSystem, 
			lightingComponent->sh, lightingComponent->specular);
		lightingDescriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms), 2);
		SET_RESOURCE_DEBUG_NAME(lightingDescriptorSet,
			"descriptorSet.translucent.lighting");
		lightingCubemap = lightingComponent->cubemap;
	}

	return GeometryRenderSystem::isDrawReady();
}

//--------------------------------------------------------------------------------------------------
type_index TranslucentRenderSystem::getComponentType() const
{
	return typeid(TranslucentRenderComponent);
}
ID<Component> TranslucentRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto instance = components.create();
	auto component = components.get(instance);
	component->transform = getManager()->getID<TransformComponent>(entity);
	return ID<Component>(instance);
}
void TranslucentRenderSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<TranslucentRenderComponent>(instance));
	destroyResources(*component);
	components.destroy(ID<TranslucentRenderComponent>(instance));
}
View<Component> TranslucentRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(
		ID<TranslucentRenderComponent>(instance)));
}
void TranslucentRenderSystem::disposeComponents()
{
	components.dispose();
}
MeshRenderType TranslucentRenderSystem::getMeshRenderType() const
{
	return MeshRenderType::Translucent;
}
const LinearPool<MeshRenderComponent>&
	TranslucentRenderSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize TranslucentRenderSystem::getMeshComponentSize() const
{
	return sizeof(TranslucentRenderComponent);
}

//--------------------------------------------------------------------------------------------------
map<string, DescriptorSet::Uniform> TranslucentRenderSystem::getBaseUniforms()
{
	auto graphicsSystem = getGraphicsSystem();
	auto lightingSystem = getManager()->get<LightingRenderSystem>();
	auto dfgImageView = graphicsSystem->get(lightingSystem->getDfgLUT());
	map<string, DescriptorSet::Uniform> constantsUniforms =
	{
		{ "cc", DescriptorSet::Uniform(graphicsSystem->getCameraConstantsBuffers()) },
		{ "instance", DescriptorSet::Uniform(instanceBuffers) },
		{ "dfgLUT", DescriptorSet::Uniform(dfgImageView->getDefaultView(), 
			1, graphicsSystem->getSwapchainSize()) },
	};
	return constantsUniforms;
}

void TranslucentRenderSystem::appendDescriptorData(Pipeline::DescriptorData* data,
	uint8& index, GeometryRenderComponent* geometryComponent)
{
	data[index++] = Pipeline::DescriptorData(lightingDescriptorSet);
}
ID<GraphicsPipeline> TranslucentRenderSystem::createPipeline()
{
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"geometry/translucent", DeferredRenderSystem::getInstance()->getHdrFramebuffer(), true);
}
*/
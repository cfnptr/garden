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

#include "garden/system/graphics/geometry/opaque.hpp"
#include "garden/system/graphics/editor/geometry.hpp"
#include "garden/system/graphics/shadow-mapping.hpp"
#include "garden/system/graphics/editor.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

#if GARDEN_EDITOR
//--------------------------------------------------------------------------------------------------
static void onOpaqueEntityInspector(ID<Entity> entity,
	Manager* manager, GeometryEditor* editor)
{
	if (ImGui::CollapsingHeader("Opaque Render"))
	{
		auto geometryComponent = manager->get<OpaqueRenderComponent>(entity);
		editor->renderInfo(*geometryComponent, nullptr);
		ImGui::Spacing();
	}
}

//--------------------------------------------------------------------------------------------------
static void onOpaqueShadowEntityInspector(ID<Entity> entity,
	Manager* manager, GeometryShadowEditor* editor)
{
	if (ImGui::CollapsingHeader("Opaque Shadow Render"))
	{
		auto geometryComponent = manager->get<OpaqueShadowRenderComponent>(entity);
		editor->renderInfo(*geometryComponent);
		ImGui::Spacing();
	}
}
#endif

void OpaqueRenderSystem::initialize()
{
	GeometryRenderSystem::initialize();

	#if GARDEN_EDITOR
	auto editorSystem = getManager()->get<EditorRenderSystem>();
	editorSystem->registerEntityInspector(
		typeid(OpaqueRenderComponent), [this](ID<Entity> entity)
		{ onOpaqueEntityInspector(entity, getManager(), (GeometryEditor*)editor); });
	#endif
}

//--------------------------------------------------------------------------------------------------
type_index OpaqueRenderSystem::getComponentType() const
{
	return typeid(OpaqueRenderComponent);
}
ID<Component> OpaqueRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto instance = components.create();
	auto component = components.get(instance);
	component->entity = entity;
	component->transform = getManager()->getID<TransformComponent>(entity);
	return ID<Component>(instance);
}
void OpaqueRenderSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<OpaqueRenderComponent>(instance));
	destroyResources(*component);
	components.destroy(ID<OpaqueRenderComponent>(instance));
}
View<Component> OpaqueRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<OpaqueRenderComponent>(instance)));
}
void OpaqueRenderSystem::disposeComponents()
{
	components.dispose();
}
MeshRenderType OpaqueRenderSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
const LinearPool<MeshRenderComponent>& OpaqueRenderSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize OpaqueRenderSystem::getMeshComponentSize() const
{
	return sizeof(OpaqueRenderComponent);
}
ID<GraphicsPipeline> OpaqueRenderSystem::createPipeline()
{
	auto deferredSystem = getManager()->get<DeferredRenderSystem>();
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"geometry/opaque", deferredSystem->getGFramebuffer(), true, true);
}

//--------------------------------------------------------------------------------------------------
void OpaqueShadowRenderSystem::initialize()
{
	GeometryShadowRenderSystem::initialize();

	#if GARDEN_EDITOR
	auto editorSystem = getManager()->get<EditorRenderSystem>();
	editorSystem->registerEntityInspector(
		typeid(OpaqueShadowRenderComponent), [this](ID<Entity> entity)
		{
			onOpaqueShadowEntityInspector(entity,
				getManager(), (GeometryShadowEditor*)editor);
		});
	#endif
}

//--------------------------------------------------------------------------------------------------
type_index OpaqueShadowRenderSystem::getComponentType() const
{
	return typeid(OpaqueShadowRenderComponent);
}
ID<Component> OpaqueShadowRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto instance = components.create();
	auto component = components.get(instance);
	component->entity = entity;
	component->transform = getManager()->getID<TransformComponent>(entity);
	return ID<Component>(instance);
}
void OpaqueShadowRenderSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<OpaqueShadowRenderComponent>(instance));
	destroyResources(*component);
	components.destroy(ID<OpaqueShadowRenderComponent>(instance));
}
View<Component> OpaqueShadowRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<OpaqueShadowRenderComponent>(instance)));
}
void OpaqueShadowRenderSystem::disposeComponents()
{
	components.dispose();
}
MeshRenderType OpaqueShadowRenderSystem::getMeshRenderType() const
{
	return MeshRenderType::OpaqueShadow;
}
const LinearPool<MeshRenderComponent>& OpaqueShadowRenderSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize OpaqueShadowRenderSystem::getMeshComponentSize() const
{
	return sizeof(OpaqueShadowRenderComponent);
}
ID<GraphicsPipeline> OpaqueShadowRenderSystem::createPipeline()
{
	auto shadowMappingSystem = getManager()->get<ShadowMappingRenderSystem>();
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"geometry/opaque-shadow", shadowMappingSystem->getFramebuffers()[0], true);
}
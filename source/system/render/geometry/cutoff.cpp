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
#include "garden/system/render/geometry/cutoff.hpp"
#include "garden/system/render/editor.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		float4 baseColor;
		float4 emissive;
		float metallic;
		float roughness;
		float reflectance;
		uint32 instanceIndex;
		float alphaCutoff;
	};
}

#if GARDEN_EDITOR
//--------------------------------------------------------------------------------------------------
static void onCutoffEntityInspector(Manager* manager,
	ID<Entity> entity, GeometryEditor* editor)
{
	if (ImGui::CollapsingHeader("Cutoff Render"))
	{
		auto geometryComponent = manager->get<CutoffRenderComponent>(entity);
		editor->renderInfo(*geometryComponent, &geometryComponent->alphaCutoff);
		ImGui::Spacing();
	}
}
#endif

//--------------------------------------------------------------------------------------------------
void CutoffRenderSystem::initialize()
{
	GeometryRenderSystem::initialize();

	#if GARDEN_EDITOR
	auto editorSystem = getManager()->get<EditorRenderSystem>();
	editorSystem->registerEntityInspector(typeid(CutoffRenderComponent),
	[this](ID<Entity> entity)
	{
		onCutoffEntityInspector(getManager(),
			entity, (GeometryEditor*)editor);
	});
	#endif
}

void CutoffRenderSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto cutoffComponent = (CutoffRenderComponent*)meshRenderComponent;
	auto pushConstants = pipelineView->getPushConstantsAsync<PushConstants>(taskIndex);
	pushConstants->alphaCutoff = cutoffComponent->alphaCutoff;
	GeometryRenderSystem::draw(meshRenderComponent, viewProj, model, drawIndex, taskIndex);
}

//--------------------------------------------------------------------------------------------------
type_index CutoffRenderSystem::getComponentType() const
{
	return typeid(CutoffRenderComponent);
}
ID<Component> CutoffRenderSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto instance = components.create();
	auto component = components.get(instance);
	component->transform = getManager()->getID<TransformComponent>(entity);
	return ID<Component>(instance);
}
void CutoffRenderSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<CutoffRenderComponent>(instance));
	destroyResources(*component);
	components.destroy(ID<CutoffRenderComponent>(instance));
}
View<Component> CutoffRenderSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CutoffRenderComponent>(instance)));
}
void CutoffRenderSystem::disposeComponents()
{
	components.dispose();
}
MeshRenderType CutoffRenderSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
const LinearPool<MeshRenderComponent>& CutoffRenderSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize CutoffRenderSystem::getMeshComponentSize() const
{
	return sizeof(CutoffRenderComponent);
}
ID<GraphicsPipeline> CutoffRenderSystem::createPipeline()
{
	auto deferredSystem = getManager()->get<DeferredRenderSystem>();
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"geometry/cutoff", deferredSystem->getGFramebuffer(), true, true);
}
*/
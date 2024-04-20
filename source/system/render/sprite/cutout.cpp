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

#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct PushConstants final
	{
		uint32 instanceIndex;
		float alphaCutoff;
	};
}

//**********************************************************************************************************************
CutoutSpriteSystem::CutoutSpriteSystem(Manager* manager, bool useDeferredBuffer) : SpriteRenderSystem(manager)
{
	this->deferredBuffer = useDeferredBuffer;
}

void CutoutSpriteSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto cutoutRenderComponent = (CutoutSpriteComponent*)meshRenderComponent;
	auto pushConstants = pipelineView->getPushConstantsAsync<PushConstants>(taskIndex);
	pushConstants->alphaCutoff = cutoutRenderComponent->alphaCutoff;
	SpriteRenderSystem::draw(meshRenderComponent, viewProj, model, drawIndex, taskIndex);
}

//**********************************************************************************************************************
type_index CutoutSpriteSystem::getComponentType() const
{
	return typeid(CutoutSpriteComponent);
}
ID<Component> CutoutSpriteSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto instance = components.create();
	auto component = components.get(instance);
	component->transform = getManager()->getID<TransformComponent>(entity);
	return ID<Component>(instance);
}
void CutoutSpriteSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<CutoutSpriteComponent>(instance));
	destroyResources(*component);
	components.destroy(ID<CutoutSpriteComponent>(instance));
}
View<Component> CutoutSpriteSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CutoutSpriteComponent>(instance)));
}
void CutoutSpriteSystem::disposeComponents() { components.dispose(); }

MeshRenderType CutoutSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
const LinearPool<MeshRenderComponent>& CutoutSpriteSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize CutoutSpriteSystem::getMeshComponentSize() const
{
	return sizeof(CutoutSpriteComponent);
}

ID<GraphicsPipeline> CutoutSpriteSystem::createPipeline()
{
	ID<Framebuffer> framebuffer;
	if (deferredBuffer)
	{
		auto deferredSystem = getManager()->get<DeferredRenderSystem>();
		framebuffer = deferredSystem->getGFramebuffer();
	}
	else
	{
		auto deferredSystem = getManager()->get<ForwardRenderSystem>();
		framebuffer = deferredSystem->getFramebuffer();
	}
	
	return ResourceSystem::getInstance()->loadGraphicsPipeline(
		"sprite/cutout", framebuffer, true, true);
}
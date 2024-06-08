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
		float4 colorFactor;
		uint32 instanceIndex;
		float alphaCutoff;
	};
}

//**********************************************************************************************************************
CutoutSpriteSystem::CutoutSpriteSystem(bool useDeferredBuffer, bool useLinearFilter)
{
	this->deferredBuffer = useDeferredBuffer;
	this->linearFilter = useLinearFilter;
}

void CutoutSpriteSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto cutoutRenderComponent = (CutoutSpriteComponent*)meshRenderComponent;
	auto pushConstants = pipelineView->getPushConstantsAsync<PushConstants>(taskIndex);
	pushConstants->colorFactor = cutoutRenderComponent->colorFactor;
	pushConstants->instanceIndex = drawIndex;
	pushConstants->alphaCutoff = cutoutRenderComponent->alphaCutoff;
	pipelineView->pushConstantsAsync(taskIndex);

	SpriteRenderSystem::draw(meshRenderComponent, viewProj, model, drawIndex, taskIndex);
}

//**********************************************************************************************************************
const string& CutoutSpriteSystem::getComponentName() const
{
	static const string name = "Cutout Sprite";
	return name;
}
type_index CutoutSpriteSystem::getComponentType() const
{
	return typeid(CutoutSpriteComponent);
}
ID<Component> CutoutSpriteSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
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
		framebuffer = DeferredRenderSystem::getInstance()->getGFramebuffer();
	else
		framebuffer = ForwardRenderSystem::getInstance()->getFramebuffer();

	map<string, GraphicsPipeline::SamplerState> samplerStateOverrides;
	if (!linearFilter)
	{
		GraphicsPipeline::SamplerState samplerState;
		samplerState.wrapX = samplerState.wrapY = samplerState.wrapZ =
			GraphicsPipeline::SamplerWrap::Repeat;
		samplerStateOverrides.emplace("colorMap", samplerState);
	}

	return ResourceSystem::getInstance()->loadGraphicsPipeline("sprite/cutout",
		framebuffer, true, true, 0, 0, {}, samplerStateOverrides, {});
}

//**********************************************************************************************************************
void CutoutSpriteSystem::serialize(ISerializer& serializer, ID<Entity> entity, ID<Component> component)
{
	auto spriteComponent = components.get(ID<CutoutSpriteComponent>(component));
	if (spriteComponent->aabb != Aabb::one)
		serializer.write("aabb", spriteComponent->aabb);
	if (spriteComponent->isEnabled != true)
		serializer.write("isEnabled", spriteComponent->isEnabled);
	if (spriteComponent->colorFactor != float4(1.0f))
		serializer.write("colorFactor", spriteComponent->colorFactor);
	if (spriteComponent->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", spriteComponent->alphaCutoff);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("path", spriteComponent->path);
	#endif
}
void CutoutSpriteSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto spriteComponent = View<CutoutSpriteComponent>(component);
	deserializer.read("aabb", spriteComponent->aabb);
	deserializer.read("isEnabled", spriteComponent->isEnabled);
	deserializer.read("colorFactor", spriteComponent->colorFactor);
	deserializer.read("alphaCutoff", spriteComponent->alphaCutoff);
	deserializer.read("path", spriteComponent->path);

	if (spriteComponent->path.empty())
		spriteComponent->path = "missing";
	spriteComponent->colorMap = ResourceSystem::getInstance()->loadImage(
		spriteComponent->path, Image::Bind::TransferDst | Image::Bind::Sampled);
}
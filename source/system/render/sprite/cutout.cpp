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
		float colorMapLayer;
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
	pushConstants->instanceIndex = drawIndex;
	pushConstants->colorMapLayer = cutoutRenderComponent->colorMapLayer;
	pushConstants->alphaCutoff = cutoutRenderComponent->alphaCutoff;
	pipelineView->pushConstantsAsync(taskIndex);

	SpriteRenderSystem::draw(meshRenderComponent, viewProj, model, drawIndex, taskIndex);
}

//**********************************************************************************************************************
ID<Component> CutoutSpriteSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void CutoutSpriteSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<CutoutSpriteComponent>(instance));
	tryDestroyResources(View<SpriteRenderComponent>(component));
	components.destroy(ID<CutoutSpriteComponent>(instance));
}
void CutoutSpriteSystem::copyComponent(View<Component> source, View<Component> destination)
{
	SpriteRenderSystem::copyComponent(View<SpriteRenderComponent>(source),
		View<SpriteRenderComponent>(destination));
	auto destinationView = View<CutoutSpriteComponent>(destination);
	const auto sourceView = View<CutoutSpriteComponent>(source);
	destinationView->alphaCutoff = sourceView->alphaCutoff;
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
void CutoutSpriteSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, entity, View<SpriteRenderComponent>(component));
	auto componentView = View<CutoutSpriteComponent>(component);
	if (componentView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", componentView->alphaCutoff);
}
void CutoutSpriteSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, entity, View<SpriteRenderComponent>(component));
	auto componentView = View<CutoutSpriteComponent>(component);
	deserializer.read("alphaCutoff", componentView->alphaCutoff);
}

//**********************************************************************************************************************
void CutoutSpriteSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, View<SpriteRenderFrame>(frame));
	auto frameView = View<CutoutSpriteFrame>(frame);
	if (frameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", frameView->alphaCutoff);
}
ID<AnimationFrame> CutoutSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	CutoutSpriteFrame frame;
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);
	frame.animateAlphaCutoff = deserializer.read("alphaCutoff", frame.alphaCutoff);

	if (frame.animateIsEnabled || frame.animateColorFactor ||
		frame.animateUvSize || frame.animateUvOffset ||
		frame.animateColorMapLayer || frame.animateAlphaCutoff)
	{
		auto instance = animationFrames.create();
		auto frameView = animationFrames.get(instance);
		**frameView = frame;
		return ID<AnimationFrame>(instance);
	}

	return {};
}
View<AnimationFrame> CutoutSpriteSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<CutoutSpriteFrame>(frame)));
}
void CutoutSpriteSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<CutoutSpriteFrame>(frame));
}

//**********************************************************************************************************************
void CutoutSpriteSystem::animateAsync(ID<Entity> entity, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto cutoutComponent = Manager::getInstance()->tryGet<CutoutSpriteComponent>(entity);
	if (!cutoutComponent)
		return;

	auto frameA = View<CutoutSpriteFrame>(a);
	auto frameB = View<CutoutSpriteFrame>(b);

	if (frameA->animateColorFactor)
		cutoutComponent->colorFactor = lerp(frameA->colorFactor, frameB->colorFactor, t);
	if (frameA->animateUvSize)
		cutoutComponent->uvSize = lerp(frameA->uvSize, frameB->uvSize, t);
	if (frameA->animateUvOffset)
		cutoutComponent->uvOffset = lerp(frameA->uvOffset, frameB->uvOffset, t);
	if (frameA->animateColorMapLayer)
		cutoutComponent->colorMapLayer = lerp(frameA->colorMapLayer, frameB->colorMapLayer, t);
	if (frameA->animateIsEnabled)
		cutoutComponent->isEnabled = (bool)round(t);
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
View<Component> CutoutSpriteSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CutoutSpriteComponent>(instance)));
}
void CutoutSpriteSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

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
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
	CutoutInstanceData instance;
	instance.mvp = viewProj * model;
	instance.colorFactor = cutoutRenderComponent->colorFactor;
	instance.sizeOffset = float4(cutoutRenderComponent->uvSize, cutoutRenderComponent->uvOffset);
	((CutoutInstanceData*)instanceMap)[drawIndex] = instance;

	auto pushConstants = pipelineView->getPushConstantsAsync<PushConstants>(taskIndex);
	pushConstants->instanceIndex = drawIndex;
	pushConstants->colorMapLayer = cutoutRenderComponent->colorMapLayer;
	pushConstants->alphaCutoff = cutoutRenderComponent->alphaCutoff;
	pipelineView->pushConstantsAsync(taskIndex);

	DescriptorSet::Range descriptorSetRange[2]; uint8 descriptorSetCount = 0;
	setDescriptorSetRange(meshRenderComponent, descriptorSetRange, descriptorSetCount, 2);
	pipelineView->bindDescriptorSetsAsync(descriptorSetRange, descriptorSetCount, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6);
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
void CutoutSpriteSystem::copyComponent(ID<Component> source, ID<Component> destination)
{
	const auto sourceView = components.get(ID<CutoutSpriteComponent>(source));
	auto destinationView = components.get(ID<CutoutSpriteComponent>(destination));
	destinationView->aabb = sourceView->aabb;
	destinationView->isEnabled = sourceView->isEnabled;
	destinationView->isArray = sourceView->isArray;
	destinationView->colorMap = sourceView->colorMap;
	destinationView->descriptorSet = sourceView->descriptorSet;
	destinationView->colorMapLayer = sourceView->colorMapLayer;
	destinationView->colorFactor = sourceView->colorFactor;
	destinationView->uvSize = sourceView->uvSize;
	destinationView->uvOffset = sourceView->uvOffset;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->path = sourceView->path;
	#endif
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
	auto spriteComponent = View<CutoutSpriteComponent>(component);
	if (spriteComponent->isArray != false)
		serializer.write("isArray", spriteComponent->isArray);
	if (spriteComponent->aabb != Aabb::one)
		serializer.write("aabb", spriteComponent->aabb);
	if (spriteComponent->isEnabled != true)
		serializer.write("isEnabled", spriteComponent->isEnabled);
	if (spriteComponent->colorMapLayer != 0.0f)
		serializer.write("colorMapLayer", spriteComponent->colorMapLayer);
	if (spriteComponent->colorFactor != float4(1.0f))
		serializer.write("colorFactor", spriteComponent->colorFactor);
	if (spriteComponent->uvSize != float2(1.0f))
		serializer.write("uvSize", spriteComponent->uvSize);
	if (spriteComponent->uvOffset != float2(0.0f))
		serializer.write("uvOffset", spriteComponent->uvOffset);
	if (spriteComponent->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", spriteComponent->alphaCutoff);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	serializer.write("path", spriteComponent->path);
	#endif
}
void CutoutSpriteSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto spriteComponent = View<CutoutSpriteComponent>(component);
	deserializer.read("isArray", spriteComponent->isArray);
	deserializer.read("aabb", spriteComponent->aabb);
	deserializer.read("isEnabled", spriteComponent->isEnabled);
	deserializer.read("colorMapLayer", spriteComponent->colorMapLayer);
	deserializer.read("colorFactor", spriteComponent->colorFactor);
	deserializer.read("uvSize", spriteComponent->uvSize);
	deserializer.read("uvOffset", spriteComponent->uvOffset);
	deserializer.read("alphaCutoff", spriteComponent->alphaCutoff);
	deserializer.read("path", spriteComponent->path);

	if (spriteComponent->path.empty())
		spriteComponent->path = "missing";
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (spriteComponent->isArray)
		flags |= ImageLoadFlags::LoadArray;
	spriteComponent->colorMap = ResourceSystem::getInstance()->loadImage(spriteComponent->path,
		Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
}

//**********************************************************************************************************************
void CutoutSpriteSystem::serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame)
{
	auto cutoutSpriteFrame = animationFrames.get(ID<CutoutSpriteFrame>(frame));
	if (cutoutSpriteFrame->animateIsEnabled)
		serializer.write("isEnabled", cutoutSpriteFrame->isEnabled);
	if (cutoutSpriteFrame->animateColorFactor)
		serializer.write("colorFactor", cutoutSpriteFrame->colorFactor);
	if (cutoutSpriteFrame->animateUvSize)
		serializer.write("uvSize", cutoutSpriteFrame->uvSize);
	if (cutoutSpriteFrame->animateUvOffset)
		serializer.write("uvOffset", cutoutSpriteFrame->uvOffset);
	if (cutoutSpriteFrame->animateColorMapLayer)
		serializer.write("colorMapLayer", cutoutSpriteFrame->colorMapLayer);
	if (cutoutSpriteFrame->animateAlphaCutoff)
		serializer.write("alphaCutoff", cutoutSpriteFrame->alphaCutoff);
}
ID<AnimationFrame> CutoutSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	CutoutSpriteFrame cutoutSpriteFrame;
	cutoutSpriteFrame.animateIsEnabled = deserializer.read("isEnabled", cutoutSpriteFrame.isEnabled);
	cutoutSpriteFrame.animateColorFactor = deserializer.read("colorFactor", cutoutSpriteFrame.colorFactor);
	cutoutSpriteFrame.animateUvSize = deserializer.read("uvSize", cutoutSpriteFrame.uvSize);
	cutoutSpriteFrame.animateUvOffset = deserializer.read("uvOffset", cutoutSpriteFrame.uvOffset);
	cutoutSpriteFrame.animateColorMapLayer = deserializer.read("colorMapLayer", cutoutSpriteFrame.colorMapLayer);
	cutoutSpriteFrame.animateAlphaCutoff = deserializer.read("alphaCutoff", cutoutSpriteFrame.alphaCutoff);

	if (cutoutSpriteFrame.animateIsEnabled || cutoutSpriteFrame.animateColorFactor ||
		cutoutSpriteFrame.animateUvSize || cutoutSpriteFrame.animateUvOffset ||
		cutoutSpriteFrame.animateColorMapLayer || cutoutSpriteFrame.animateAlphaCutoff)
	{
		auto frame = animationFrames.create();
		auto frameView = animationFrames.get(frame);
		**frameView = cutoutSpriteFrame;
		return ID<AnimationFrame>(frame);
	}

	return {};
}

//**********************************************************************************************************************
void CutoutSpriteSystem::animateAsync(ID<Entity> entity, ID<AnimationFrame> a, ID<AnimationFrame> b, float t)
{
	auto cutoutComponent = Manager::getInstance()->tryGet<CutoutSpriteComponent>(entity);
	if (!cutoutComponent)
		return;

	auto frameA = animationFrames.get(ID<CutoutSpriteFrame>(a));
	auto frameB = animationFrames.get(ID<CutoutSpriteFrame>(b));

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
void CutoutSpriteSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<CutoutSpriteFrame>(frame));
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
uint64 CutoutSpriteSystem::getInstanceDataSize()
{
	return sizeof(CutoutInstanceData);
}
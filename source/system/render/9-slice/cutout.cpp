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

#include "garden/system/render/9-slice/cutout.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
Cutout9SliceSystem::Cutout9SliceSystem(bool useDeferredBuffer, bool useLinearFilter, bool setSingleton) :
	NineSliceRenderSystem("9-slice/cutout", useDeferredBuffer, useLinearFilter), Singleton(setSingleton) { }
Cutout9SliceSystem::~Cutout9SliceSystem() { unsetSingleton(); }

void Cutout9SliceSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	SpriteRenderSystem::setPushConstants(spriteRenderView,
		pushConstants, viewProj, model, drawIndex, taskIndex);
	auto cutout9SliceView = (Cutout9SliceComponent*)spriteRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutout9SliceView->alphaCutoff;
}

//**********************************************************************************************************************
ID<Component> Cutout9SliceSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void Cutout9SliceSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<Cutout9SliceComponent>(instance));
	destroyResources(View<SpriteRenderComponent>(componentView));
	components.destroy(ID<Cutout9SliceComponent>(instance));
}
void Cutout9SliceSystem::copyComponent(View<Component> source, View<Component> destination)
{
	NineSliceRenderSystem::copyComponent(source, destination);
	auto destinationView = View<Cutout9SliceComponent>(destination);
	const auto sourceView = View<Cutout9SliceComponent>(source);
	destinationView->alphaCutoff = sourceView->alphaCutoff;
}
const string& Cutout9SliceSystem::getComponentName() const
{
	static const string name = "Cutout 9-Slice";
	return name;
}
type_index Cutout9SliceSystem::getComponentType() const
{
	return typeid(Cutout9SliceComponent);
}
View<Component> Cutout9SliceSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<Cutout9SliceComponent>(instance)));
}
void Cutout9SliceSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

MeshRenderType Cutout9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
LinearPool<MeshRenderComponent>& Cutout9SliceSystem::getMeshComponentPool()
{
	return *((LinearPool<MeshRenderComponent>*)&components);
}
psize Cutout9SliceSystem::getMeshComponentSize() const
{
	return sizeof(Cutout9SliceComponent);
}
LinearPool<SpriteRenderFrame>& Cutout9SliceSystem::getFrameComponentPool()
{
	return *((LinearPool<SpriteRenderFrame>*)&animationFrames);
}
psize Cutout9SliceSystem::getFrameComponentSize() const
{
	return sizeof(Cutout9SliceFrame);
}

//**********************************************************************************************************************
void Cutout9SliceSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	auto componentView = View<Cutout9SliceComponent>(component);
	if (componentView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", componentView->alphaCutoff);
}
void Cutout9SliceSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, entity, component);
	auto componentView = View<Cutout9SliceComponent>(component);
	deserializer.read("alphaCutoff", componentView->alphaCutoff);
}

//**********************************************************************************************************************
void Cutout9SliceSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	auto frameView = View<Cutout9SliceFrame>(frame);
	if (frameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", frameView->alphaCutoff);
}
ID<AnimationFrame> Cutout9SliceSystem::deserializeAnimation(IDeserializer& deserializer)
{
	Cutout9SliceFrame frame;
	NineSliceRenderSystem::deserializeAnimation(deserializer, frame);
	frame.animateAlphaCutoff = deserializer.read("alphaCutoff", frame.alphaCutoff);

	if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize || 
		frame.animateUvOffset || frame.animateColorMapLayer || frame.animateColorMap ||
		frame.animateAlphaCutoff || frame.animateTextureBorder || frame.animateWindowBorder)
	{
		return ID<AnimationFrame>(animationFrames.create(frame));
	}

	return {};
}
View<AnimationFrame> Cutout9SliceSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<Cutout9SliceFrame>(frame)));
}
void Cutout9SliceSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto componentView = View<Cutout9SliceComponent>(component);
	auto frameA = View<Cutout9SliceFrame>(a);
	auto frameB = View<Cutout9SliceFrame>(b);
	if (frameA->animateAlphaCutoff)
		componentView->alphaCutoff = lerp(frameA->alphaCutoff, frameB->alphaCutoff, t);
}
void Cutout9SliceSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	auto frameView = animationFrames.get(ID<Cutout9SliceFrame>(frame));
	destroyResources(View<SpriteRenderFrame>(frameView));
	animationFrames.destroy(ID<Cutout9SliceFrame>(frame));
}
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

#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
CutoutSpriteSystem::CutoutSpriteSystem(bool useDeferredBuffer, bool useLinearFilter, bool setSingleton) :
	SpriteRenderCompSystem("sprite/cutout", useDeferredBuffer, useLinearFilter, false), Singleton(setSingleton) { }
CutoutSpriteSystem::~CutoutSpriteSystem() { unsetSingleton(); }

void CutoutSpriteSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex)
{
	SpriteRenderSystem::setPushConstants(spriteRenderView,
		pushConstants, viewProj, model, drawIndex, threadIndex);
	auto cutoutSpriteView = (CutoutSpriteComponent*)spriteRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutoutSpriteView->alphaCutoff;
}

void CutoutSpriteSystem::copyComponent(View<Component> source, View<Component> destination)
{
	SpriteRenderSystem::copyComponent(source, destination);
	auto destinationView = View<CutoutSpriteComponent>(destination);
	const auto sourceView = View<CutoutSpriteComponent>(source);
	destinationView->alphaCutoff = sourceView->alphaCutoff;
}
const string& CutoutSpriteSystem::getComponentName() const
{
	static const string name = "Cutout Sprite";
	return name;
}

//**********************************************************************************************************************
void CutoutSpriteSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	auto cutoutSpriteView = View<CutoutSpriteComponent>(component);
	if (cutoutSpriteView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", cutoutSpriteView->alphaCutoff);
}
void CutoutSpriteSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, entity, component);
	auto cutoutSpriteView = View<CutoutSpriteComponent>(component);
	deserializer.read("alphaCutoff", cutoutSpriteView->alphaCutoff);
}

void CutoutSpriteSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	auto cutoutFrameView = View<CutoutSpriteFrame>(frame);
	if (cutoutFrameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", cutoutFrameView->alphaCutoff);
}
ID<AnimationFrame> CutoutSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	CutoutSpriteFrame frame;
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);
	frame.animateAlphaCutoff = deserializer.read("alphaCutoff", frame.alphaCutoff);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}
void CutoutSpriteSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto cutoutSpriteView = View<CutoutSpriteComponent>(component);
	auto frameA = View<CutoutSpriteFrame>(a);
	auto frameB = View<CutoutSpriteFrame>(b);
	if (frameA->animateAlphaCutoff)
		cutoutSpriteView->alphaCutoff = lerp(frameA->alphaCutoff, frameB->alphaCutoff, t);
}
// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

using namespace garden;

//**********************************************************************************************************************
CutoutSpriteSystem::CutoutSpriteSystem(bool setSingleton) : 
	SpriteCompAnimSystem("sprite/cutout"), Singleton(setSingleton)
{
	Manager::getInstance()->addGroupSystem<IMeshRenderSystem>(this);
}

void CutoutSpriteSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 threadIndex)
{
	SpriteRenderSystem::setPushConstants(spriteRenderView,
		pushConstants, viewProj, model, instanceIndex, threadIndex);
	auto cutoutSpriteView = (CutoutSpriteComponent*)spriteRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutoutSpriteView->getAlphaCutoff();
}

string_view CutoutSpriteSystem::getComponentName() const
{
	return "Cutout Sprite";
}
MeshRenderType CutoutSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Color;
}

//**********************************************************************************************************************
void CutoutSpriteSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	const auto componentView = View<CutoutSpriteComponent>(component);
	if (componentView->getAlphaCutoff() != 0.5f)
		serializer.write("alphaCutoff", componentView->getAlphaCutoff());
}
void CutoutSpriteSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, component);
	auto componentView = View<CutoutSpriteComponent>(component);

	float alphaCutoff = componentView->getAlphaCutoff();
	if (deserializer.read("alphaCutoff", alphaCutoff))
		componentView->setAlphaCutoff(alphaCutoff);
}

void CutoutSpriteSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	const auto frameView = View<CutoutSpriteFrame>(frame);
	if (frameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", frameView->alphaCutoff);
}
void CutoutSpriteSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);
	auto frameView = View<CutoutSpriteFrame>(frame);
	frameView->animateAlphaCutoff = deserializer.read("alphaCutoff", frameView->alphaCutoff);
}
void CutoutSpriteSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);

	auto componentView = View<CutoutSpriteComponent>(component);
	const auto frameA = View<CutoutSpriteFrame>(a);
	const auto frameB = View<CutoutSpriteFrame>(b);
	if (frameA->animateAlphaCutoff)
		componentView->setAlphaCutoff(lerp(frameA->alphaCutoff, frameB->alphaCutoff, t));
}
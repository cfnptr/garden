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

#include "garden/system/render/9-slice/cutout.hpp"

using namespace garden;

//**********************************************************************************************************************
Cutout9SliceSystem::Cutout9SliceSystem(bool setSingleton) : 
	NineSliceCompAnimSystem("9-slice/cutout"), Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<IMeshRenderSystem>(this);
}
Cutout9SliceSystem::~Cutout9SliceSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<IMeshRenderSystem>(this);
	unsetSingleton();
}

void Cutout9SliceSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 threadIndex)
{
	SpriteRenderSystem::setPushConstants(spriteRenderView,
		pushConstants, viewProj, model, instanceIndex, threadIndex);
	auto cutout9SliceView = (Cutout9SliceComponent*)spriteRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutout9SliceView->alphaCutoff;
}

string_view Cutout9SliceSystem::getComponentName() const
{
	return "Cutout 9-Slice";
}
MeshRenderType Cutout9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}

//**********************************************************************************************************************
void Cutout9SliceSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	const auto componentView = View<Cutout9SliceComponent>(component);
	if (componentView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", componentView->alphaCutoff);
}
void Cutout9SliceSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, component);
	auto componentView = View<Cutout9SliceComponent>(component);
	deserializer.read("alphaCutoff", componentView->alphaCutoff);
}

void Cutout9SliceSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	const auto frameView = View<Cutout9SliceFrame>(frame);
	if (frameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", frameView->alphaCutoff);
}
void Cutout9SliceSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	NineSliceRenderSystem::deserializeAnimation(deserializer, frame);
	auto frameView = View<Cutout9SliceFrame>(frame);
	frameView->animateAlphaCutoff = deserializer.read("alphaCutoff", frameView->alphaCutoff);
}
void Cutout9SliceSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto componentView = View<Cutout9SliceComponent>(component);
	const auto frameA = View<Cutout9SliceFrame>(a);
	const auto frameB = View<Cutout9SliceFrame>(b);
	if (frameA->animateAlphaCutoff)
		componentView->alphaCutoff = lerp(frameA->alphaCutoff, frameB->alphaCutoff, t);
}
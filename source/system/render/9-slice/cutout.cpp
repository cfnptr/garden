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
Cutout9SliceSystem::Cutout9SliceSystem(bool setSingleton) : NineSliceRenderCompSystem(
	"9-slice/cutout"), Singleton(setSingleton) { }
Cutout9SliceSystem::~Cutout9SliceSystem() { unsetSingleton(); }

void Cutout9SliceSystem::setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex)
{
	SpriteRenderSystem::setPushConstants(spriteRenderView,
		pushConstants, viewProj, model, drawIndex, threadIndex);
	auto cutout9SliceView = (Cutout9SliceComponent*)spriteRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutout9SliceView->alphaCutoff;
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
MeshRenderType Cutout9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}

//**********************************************************************************************************************
void Cutout9SliceSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	auto cutout9SliceView = View<Cutout9SliceComponent>(component);
	if (cutout9SliceView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", cutout9SliceView->alphaCutoff);
}
void Cutout9SliceSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, entity, component);
	auto cutout9SliceView = View<Cutout9SliceComponent>(component);
	deserializer.read("alphaCutoff", cutout9SliceView->alphaCutoff);
}

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

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}
void Cutout9SliceSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto cutout9SliceView = View<Cutout9SliceComponent>(component);
	auto frameA = View<Cutout9SliceFrame>(a);
	auto frameB = View<Cutout9SliceFrame>(b);
	if (frameA->animateAlphaCutoff)
		cutout9SliceView->alphaCutoff = lerp(frameA->alphaCutoff, frameB->alphaCutoff, t);
}
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

#include "garden/system/render/model/cutout.hpp"
#include "garden/system/render/mesh.hpp"

using namespace garden;

//**********************************************************************************************************************
CutoutModelSystem::CutoutModelSystem(bool useNormalMapping,  bool setSingleton) : ModelRenderCompSystem(
	useNormalMapping ? "model/cutout" : "model/cutout-lite", useNormalMapping, true), Singleton(setSingleton) { }
CutoutModelSystem::~CutoutModelSystem() { unsetSingleton(); }

void CutoutModelSystem::setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex)
{
	ModelRenderSystem::setPushConstants(modelRenderView,
		pushConstants, viewProj, model, drawIndex, threadIndex);
	auto cutoutModelView = (CutoutModelComponent*)modelRenderView;
	auto cutoutPushConstants = (CutoutPushConstants*)pushConstants;
	cutoutPushConstants->alphaCutoff = cutoutModelView->alphaCutoff;
}

void CutoutModelSystem::copyComponent(View<Component> source, View<Component> destination)
{
	ModelRenderSystem::copyComponent(source, destination);
	auto destinationView = View<CutoutModelComponent>(destination);
	const auto sourceView = View<CutoutModelComponent>(source);
	destinationView->alphaCutoff = sourceView->alphaCutoff;
}
const string& CutoutModelSystem::getComponentName() const
{
	static const string name = "Cutout Model";
	return name;
}
MeshRenderType CutoutModelSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}

//**********************************************************************************************************************
void CutoutModelSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	ModelRenderSystem::serialize(serializer, component);
	auto cutoutModelView = View<CutoutModelComponent>(component);
	if (cutoutModelView->alphaCutoff != 0.5f)
		serializer.write("alphaCutoff", cutoutModelView->alphaCutoff);
}
void CutoutModelSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	ModelRenderSystem::deserialize(deserializer, entity, component);
	auto cutoutModelView = View<CutoutModelComponent>(component);
	deserializer.read("alphaCutoff", cutoutModelView->alphaCutoff);
}

void CutoutModelSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	ModelRenderSystem::serializeAnimation(serializer, frame);
	auto cutoutFrameView = View<CutoutModelFrame>(frame);
	if (cutoutFrameView->animateAlphaCutoff)
		serializer.write("alphaCutoff", cutoutFrameView->alphaCutoff);
}
ID<AnimationFrame> CutoutModelSystem::deserializeAnimation(IDeserializer& deserializer)
{
	CutoutModelFrame frame;
	ModelRenderSystem::deserializeAnimation(deserializer, frame);
	frame.animateAlphaCutoff = deserializer.read("alphaCutoff", frame.alphaCutoff);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}
void CutoutModelSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	ModelRenderSystem::animateAsync(component, a, b, t);
	auto cutoutModelView = View<CutoutModelComponent>(component);
	auto frameA = View<CutoutModelFrame>(a);
	auto frameB = View<CutoutModelFrame>(b);
	if (frameA->animateAlphaCutoff)
		cutoutModelView->alphaCutoff = lerp(frameA->alphaCutoff, frameB->alphaCutoff, t);
}
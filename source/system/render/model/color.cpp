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

#include "garden/system/render/model/color.hpp"
#include "math/simd/vector/float.hpp"

using namespace garden;

//**********************************************************************************************************************
ColorModelSystem::ColorModelSystem(bool setSingleton) : ModelRenderCompSystem(
	"model/color", false, false), Singleton(setSingleton) { }
ColorModelSystem::~ColorModelSystem() { unsetSingleton(); }

void ColorModelSystem::copyComponent(View<Component> source, View<Component> destination)
{
	ModelRenderSystem::copyComponent(source, destination);
	auto destinationView = View<ColorModelComponent>(destination);
	const auto sourceView = View<ColorModelComponent>(source);
	destinationView->color = sourceView->color;
}
const string& ColorModelSystem::getComponentName() const
{
	static const string name = "Color Model";
	return name;
}
MeshRenderType ColorModelSystem::getMeshRenderType() const
{
	return MeshRenderType::Color;
}

//**********************************************************************************************************************
void ColorModelSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	ModelRenderSystem::serialize(serializer, component);
	auto colorModelView = View<ColorModelComponent>(component);
	if (colorModelView->color != f32x4::one)
		serializer.write("color", (float4)colorModelView->color);
}
void ColorModelSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	ModelRenderSystem::deserialize(deserializer, entity, component);
	auto colorModelView = View<ColorModelComponent>(component);
	deserializer.read("color", colorModelView->color);
}

void ColorModelSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	ModelRenderSystem::serializeAnimation(serializer, frame);
	auto colorFrameView = View<ColorModelFrame>(frame);
	if (colorFrameView->animateColor)
		serializer.write("color", (float4)colorFrameView->color);
}
ID<AnimationFrame> ColorModelSystem::deserializeAnimation(IDeserializer& deserializer)
{
	ColorModelFrame frame;
	ModelRenderSystem::deserializeAnimation(deserializer, frame);
	frame.animateColor = deserializer.read("color", frame.color);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}
void ColorModelSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	ModelRenderSystem::animateAsync(component, a, b, t);
	auto colorModelView = View<ColorModelComponent>(component);
	auto frameA = View<ColorModelFrame>(a);
	auto frameB = View<ColorModelFrame>(b);
	if (frameA->animateColor)
		colorModelView->color = lerp(frameA->color, frameB->color, t);
}
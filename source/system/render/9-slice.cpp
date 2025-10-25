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

#include "garden/system/render/9-slice.hpp"
#include "math/matrix/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
void NineSliceRenderSystem::resetComponent(View<Component> component, bool full)
{
	SpriteRenderSystem::resetComponent(component, full);
	if (full)
	{
		auto nineSliceView = View<NineSliceComponent>(component);
		nineSliceView->textureBorder = float2::zero;
		nineSliceView->windowBorder = float2::zero;
	}
}
void NineSliceRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	SpriteRenderSystem::copyComponent(source, destination);
	auto destinationView = View<NineSliceComponent>(destination);
	const auto sourceView = View<NineSliceComponent>(source);
	destinationView->textureBorder = sourceView->textureBorder;
	destinationView->windowBorder = sourceView->windowBorder;
}

uint64 NineSliceRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(NineSliceInstanceData);
}
void NineSliceRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex)
{
	SpriteRenderSystem::setInstanceData(spriteRenderView,
		instanceData, viewProj, model, drawIndex, threadIndex);

	auto nineSliceView = (NineSliceComponent*)spriteRenderView;
	auto nineSliceInstanceData = (NineSliceInstanceData*)instanceData;

	auto imageSize = float2::one; // Note: White texture size.
	if (nineSliceView->colorMap)
	{
		auto imageView = GraphicsSystem::Instance::get()->get(nineSliceView->colorMap);
		imageSize = (float2)(uint2)imageView->getSize();
	}
	auto scale = imageSize / extractScale2(model);

	nineSliceInstanceData->textureBorder = nineSliceView->textureBorder / imageSize;
	nineSliceInstanceData->windowBorder = nineSliceView->windowBorder / imageSize * scale;
}

//**********************************************************************************************************************
void NineSliceRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	const auto nineSliceView = View<NineSliceComponent>(component);
	if (nineSliceView->textureBorder != float2::zero)
		serializer.write("textureBorder", nineSliceView->textureBorder);
	if (nineSliceView->windowBorder != float2::zero)
		serializer.write("windowBorder", nineSliceView->windowBorder);
}
void NineSliceRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, component);
	auto nineSliceView = View<NineSliceComponent>(component);
	deserializer.read("textureBorder", nineSliceView->textureBorder);
	deserializer.read("windowBorder", nineSliceView->windowBorder);
}

//**********************************************************************************************************************
void NineSliceRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	const auto frameView = View<NineSliceFrame>(frame);
	if (frameView->animateTextureBorder)
		serializer.write("textureBorder", frameView->textureBorder);
	if (frameView->animateWindowBorder)
		serializer.write("windowBorder", frameView->windowBorder);
}
void NineSliceRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto nineSliceView = View<NineSliceComponent>(component);
	const auto frameA = View<NineSliceFrame>(a);
	const auto frameB = View<NineSliceFrame>(b);
	if (frameA->animateTextureBorder)
		nineSliceView->textureBorder = lerp(frameA->textureBorder, frameB->textureBorder, t);
	if (frameA->animateWindowBorder)
		nineSliceView->windowBorder = lerp(frameA->windowBorder, frameB->windowBorder, t);
}
void NineSliceRenderSystem::deserializeAnimation(IDeserializer& deserializer, NineSliceFrame& frame)
{
	frame.animateTextureBorder = deserializer.read("textureBorder", frame.textureBorder);
	frame.animateWindowBorder = deserializer.read("windowBorder", frame.windowBorder);
}
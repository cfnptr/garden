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

#include "garden/system/render/9-slice.hpp"

using namespace garden;

//**********************************************************************************************************************
uint64 NineSliceRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(NineSliceInstanceData);
}
void NineSliceRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 threadIndex)
{
	SpriteRenderSystem::setInstanceData(spriteRenderView,
		instanceData, viewProj, model, instanceIndex, threadIndex);

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
	const auto componentView = View<NineSliceComponent>(component);
	if (componentView->textureBorder != float2::zero)
		serializer.write("textureBorder", componentView->textureBorder);
	if (componentView->windowBorder != float2::zero)
		serializer.write("windowBorder", componentView->windowBorder);
}
void NineSliceRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, component);
	auto componentView = View<NineSliceComponent>(component);
	deserializer.read("textureBorder", componentView->textureBorder);
	deserializer.read("windowBorder", componentView->windowBorder);
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
void NineSliceRenderSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);
	auto frameView = View<NineSliceFrame>(frame);
	frameView->animateTextureBorder = deserializer.read("textureBorder", frameView->textureBorder);
	frameView->animateWindowBorder = deserializer.read("windowBorder", frameView->windowBorder);
}
void NineSliceRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);

	auto componentView = View<NineSliceComponent>(component);
	const auto frameA = View<NineSliceFrame>(a);
	const auto frameB = View<NineSliceFrame>(b);
	if (frameA->animateTextureBorder)
		componentView->textureBorder = lerp(frameA->textureBorder, frameB->textureBorder, t);
	if (frameA->animateWindowBorder)
		componentView->windowBorder = lerp(frameA->windowBorder, frameB->windowBorder, t);
}
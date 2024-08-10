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

#include "garden/system/render/9-slice.hpp"

using namespace garden;

//**********************************************************************************************************************
void NineSliceRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	SpriteRenderSystem::copyComponent(source, destination);
	auto destinationView = View<NineSliceRenderComponent>(destination);
	const auto sourceView = View<NineSliceRenderComponent>(source);
	destinationView->textureBorder = sourceView->textureBorder;
	destinationView->windowBorder = sourceView->windowBorder;
}

uint64 NineSliceRenderSystem::getInstanceDataSize()
{
	return (uint64)sizeof(NineSliceInstanceData);
}
void NineSliceRenderSystem::setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto nineSliceRenderView = (NineSliceRenderComponent*)spriteRenderView;
	auto nineSliceInstanceData = (NineSliceInstanceData*)instanceData;

	auto imageSize = float2(1.0f); // White texture size
	if (nineSliceRenderView->colorMap)
	{
		auto imageView = GraphicsSystem::get()->get(nineSliceRenderView->colorMap);
		imageSize = (float2)(int2)imageView->getSize();
	}
	auto scale = extractScale2(model) * imageSize;

	SpriteRenderSystem::setInstanceData(spriteRenderView,
		instanceData, viewProj, model, drawIndex, taskIndex);
	nineSliceInstanceData->texWinBorder = float4(
		nineSliceRenderView->textureBorder / imageSize,
		nineSliceRenderView->windowBorder / scale);
}

//**********************************************************************************************************************
void NineSliceRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	SpriteRenderSystem::serialize(serializer, component);
	auto componentView = View<NineSliceRenderComponent>(component);
	if (componentView->textureBorder != float2(0.0f))
		serializer.write("textureBorder", componentView->textureBorder);
	if (componentView->windowBorder != float2(0.0f))
		serializer.write("windowBorder", componentView->windowBorder);
}
void NineSliceRenderSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	SpriteRenderSystem::deserialize(deserializer, entity, component);
	auto componentView = View<NineSliceRenderComponent>(component);
	deserializer.read("textureBorder", componentView->textureBorder);
	deserializer.read("windowBorder", componentView->windowBorder);
}

//**********************************************************************************************************************
void NineSliceRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	SpriteRenderSystem::serializeAnimation(serializer, frame);
	auto frameView = View<NineSliceRenderFrame>(frame);
	if (frameView->animateTextureBorder)
		serializer.write("textureBorder", frameView->textureBorder);
	if (frameView->animateWindowBorder)
		serializer.write("windowBorder", frameView->windowBorder);
}
void NineSliceRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	SpriteRenderSystem::animateAsync(component, a, b, t);
	auto componentView = View<NineSliceRenderComponent>(component);
	auto frameA = View<NineSliceRenderFrame>(a);
	auto frameB = View<NineSliceRenderFrame>(b);
	if (frameA->animateTextureBorder)
		componentView->textureBorder = lerp(frameA->textureBorder, frameB->textureBorder, t);
	if (frameA->animateWindowBorder)
		componentView->windowBorder = lerp(frameA->windowBorder, frameB->windowBorder, t);
}
void NineSliceRenderSystem::deserializeAnimation(IDeserializer& deserializer, NineSliceRenderFrame& frame)
{
	frame.animateTextureBorder = deserializer.read("textureBorder", frame.textureBorder);
	frame.animateWindowBorder = deserializer.read("windowBorder", frame.windowBorder);
}
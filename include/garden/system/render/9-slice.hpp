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

// TODO: Add center slice repeat mode instead of stretching, if needed.
//       Also add adaptive stretch mode like in Unity, if needed.

#pragma once
#include "garden/system/render/sprite.hpp"

namespace garden
{

using namespace garden::graphics;

struct NineSliceRenderComponent : public SpriteRenderComponent
{
	float2 textureBorder = float2(0.0f);
	float2 windowBorder = float2(0.0f);
};

struct NineSliceRenderFrame : public SpriteRenderFrame
{
	float2 textureBorder = float2(0.0f);
	float2 windowBorder = float2(0.0f);
	bool animateTextureBorder = false;
	bool animateWindowBorder = false;
};

class NineSliceRenderSystem : public SpriteRenderSystem
{
public:
	struct NineSliceInstanceData : public InstanceData
	{
		float4 texWinBorder = float4(0.0f);
	};
protected:
	NineSliceRenderSystem(const fs::path& pipelinePath, bool useDeferredBuffer, bool useLinearFilter) :
		SpriteRenderSystem(pipelinePath, useDeferredBuffer, useLinearFilter) { }

	void copyComponent(View<Component> source, View<Component> destination) override;

	uint64 getInstanceDataSize() override;
	void setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex) override;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, NineSliceRenderFrame& frame);
};

} // namespace garden
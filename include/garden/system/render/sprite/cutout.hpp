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

/***********************************************************************************************************************
 * @file
 * @brief Cutout sprite rendering functions.
 */

#pragma once
#include "garden/system/render/sprite.hpp"

namespace garden
{

using namespace garden::graphics;

struct CutoutSpriteComponent final : public SpriteRenderComponent
{
	float alphaCutoff = 0.5f;
};
struct CutoutSpriteFrame final : public SpriteAnimationFrame
{
	float alphaCutoff = 0.5f;
	bool animateAlphaCutoff = false;
};

class CutoutSpriteSystem final : public SpriteRenderCompSystem<
	CutoutSpriteComponent, CutoutSpriteFrame, false, false>, public Singleton<CutoutSpriteSystem>
{
public:
	struct CutoutPushConstants final : public PushConstants
	{
		float alphaCutoff;
	};
private:

	/**
	 * @brief Creates a new cutout sprite rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 * @param setSingleton set system singleton instance
	 */
	CutoutSpriteSystem(bool useDeferredBuffer = false, bool useLinearFilter = true, bool setSingleton = true);
	/**
	 * @brief Destroys cutout sprite rendering system instance.
	 */
	~CutoutSpriteSystem() final;

	void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex) final;

	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
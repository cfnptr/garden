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

/***********************************************************************************************************************
 * @file
 * @brief Cutout model rendering functions.
 */

#pragma once
#include "garden/system/render/model.hpp"

namespace garden
{

/**
 * @brief Cutout model rendering data container.
 */
struct CutoutModelComponent final : public ModelRenderComponent
{
	float alphaCutoff = 0.5f;
};
/**
 * @brief Cutout model animation frame container.
 */
struct CutoutModelFrame final : public ModelAnimationFrame
{
	float alphaCutoff = 0.5f;
	bool animateAlphaCutoff = false;

	bool hasAnimation() final
	{
		return ModelAnimationFrame::hasAnimation() || animateAlphaCutoff;
	}
};

/**
 * @brief Cutout model rendering system.
 */
class CutoutModelSystem final : public ModelRenderCompSystem<
	CutoutModelComponent, CutoutModelFrame, false, false>, public Singleton<CutoutModelSystem>
{
public:
	struct CutoutPushConstants final : public PushConstants
	{
		float alphaCutoff;
	};
private:
	/**
	 * @brief Creates a new cutout model rendering system instance.
	 * 
	 * @param useNormalMapping load and use normal map textures
	 * @param setSingleton set system singleton instance
	 */
	CutoutModelSystem(bool useNormalMapping = true, bool setSingleton = true);
	/**
	 * @brief Destroys cutout model rendering system instance.
	 */
	~CutoutModelSystem() final;

	void setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex) final;

	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
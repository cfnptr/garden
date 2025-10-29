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
 * @brief Common 9-slice sprite rendering functions. (scale 9 grid, 9-patch)
 */

// TODO: Add center slice repeat mode instead of stretching, if needed.
//       Also add adaptive stretch mode like in Unity, if needed.

#pragma once
#include "garden/system/render/sprite.hpp"
#include "garden/system/transform.hpp"

namespace garden
{

/**
 * @brief 9-slice sprite rendering data container.
 */
struct NineSliceComponent : public SpriteRenderComponent
{
	float2 textureBorder = float2::zero;
	float2 windowBorder = float2::zero;
};

/**
 * @brief 9-slice sprite animation frame container.
 */
struct NineSliceFrame : public SpriteAnimFrame
{
	float2 textureBorder = float2::zero;
	float2 windowBorder = float2::zero;
	bool animateTextureBorder = false;
	bool animateWindowBorder = false;

	bool hasAnimation() override
	{
		return SpriteAnimFrame::hasAnimation() || animateTextureBorder || animateWindowBorder;
	}
};

/***********************************************************************************************************************
 * @brief 9-slice sprite rendering system. (Scale 9 grid, 9-patch)
 */
class NineSliceRenderSystem : public SpriteRenderSystem
{
public:
	struct NineSliceInstanceData : public BaseInstanceData
	{
		float2 textureBorder = float2::zero;
		float2 windowBorder = float2::zero;
	};
protected:
	/**
	 * @brief Creates a new 9-slice sprite render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	NineSliceRenderSystem(const fs::path& pipelinePath) : SpriteRenderSystem(pipelinePath) { }

	uint64 getBaseInstanceDataSize() override;
	void setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex) override;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, View<Component> component) override;

	static void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame);
	static void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame);
	static void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t);
};

/***********************************************************************************************************************
 * @brief 9-slice sprite rendering system with components and animation frames.
 * @details See the @ref NineSliceRenderSystem.
 *
 * @tparam C type of the system component
 * @tparam F type of the system animation frame
 *
 * @tparam DestroyComponents system should call destroy() function of the components
 * @tparam DestroyAnimationFrames system should call destroy() function of the animation frames
 */
template<class C = Component, class F = AnimationFrame, 
	bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class NineSliceCompAnimSystem : public CompAnimSystem<C, F, 
	DestroyComponents, DestroyAnimationFrames>, public NineSliceRenderSystem
{
protected:
	/**
	 * @brief Creates a new 9-slice sprite render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	NineSliceCompAnimSystem(const fs::path& pipelinePath) : NineSliceRenderSystem(pipelinePath) { }
	/**
	 * @brief Destroys 9-slice sprite render system instance.
	 */
	~NineSliceCompAnimSystem() override { }

	void resetComponent(View<Component> component, bool full) override
	{ NineSliceRenderSystem::resetComponent(component); if (full) **View<C>(component) = C(); }

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override
	{ NineSliceRenderSystem::serializeAnimation(serializer, frame); }
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) override
	{ NineSliceRenderSystem::deserializeAnimation(deserializer, frame); }
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) override
	{ NineSliceRenderSystem::animateAsync(component, a, b, t); }
	void resetAnimation(View<AnimationFrame> frame, bool full) override
	{ NineSliceRenderSystem::resetAnimation(frame); if (full) **View<F>(frame) = F(); }

	MeshRenderPool& getMeshComponentPool() override { return *((MeshRenderPool*)&this->components); }
	psize getMeshComponentSize() const override { return sizeof(C); }
	SpriteFramePool& getSpriteFramePool() override { return *((SpriteFramePool*)&this->animationFrames); }
	psize getSpriteFrameSize() const override { return sizeof(F); }
};

} // namespace garden
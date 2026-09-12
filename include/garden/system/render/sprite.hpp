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

/***********************************************************************************************************************
 * @file
 * @brief Common sprite rendering functions.
 */

#pragma once
#include "garden/system/render/instance.hpp"

namespace garden
{

class SpriteRenderSystem;

/**
 * @brief Sprite rendering data container.
 */
struct SpriteRenderComponent : public MeshRenderComponent
{
	Ref<Image> colorMap = {};              /**< Color map texture instance. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Descriptor set instance. */
	half2 uvSize = half2::one;             /**< Texture UV size. */
	half2 uvOffset = half2::zero;          /**< Texture UV offset. */
	Color colorAdd = Color::transparent;   /**< Added to the color map. */
	Color colorMul = Color::white;         /**< Multiplied by the color map. */
	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = "";            /**< Color map texture path. */
	#endif

	/**
	 * @brief Creates a new sprite rendering data container.
	 */
	SpriteRenderComponent() { setColorMapLayer(0.0f); }

	/**
	 * @brief Returns sprite texture UV size.
	 */
	half2 getUvSize() const noexcept { return *((const half2*)&colorMap.unused); }
	/**
	 * @brief Sets sprite texture UV size.
	 * @param size target texture UV size
	 */
	void setUvSize(half2 size) noexcept { _uvSize() = size; }

	/**
	 * @brief Returns sprite texture UV offset.
	 */
	half2 getUvOffset() const noexcept { return *((const half2*)&descriptorSet.unused); }
	/**
	 * @brief Sets sprite texture UV offset.
	 * @param offset target texture UV offset
	 */
	void setUvOffset(half2 offset) noexcept { _uvOffset() = offset; }

	/**
	 * @brief Returns sprite color map layer.
	 */
	float getColorMapLayer() const noexcept { return *((const float*)&unused0); }
	/**
	 * @brief Sets sprite color map layer.
	 * @param layer target color map layer
	 */
	void setColorMapLayer(float layer) noexcept { _colorMapLayer() = layer; }

protected:
	half2& _uvSize() noexcept { return *((half2*)&colorMap.unused); }
	half2& _uvOffset() noexcept { return *((half2*)&descriptorSet.unused); }
	float& _colorMapLayer() noexcept { return *((float*)&unused0); }

	friend class SpriteRenderSystem;
};

/**
 * @brief Sprite animation frame container.
 */
struct SpriteAnimFrame : public AnimationFrame
{
	uint8 animateIsEnabled : 1;
	uint8 animateUvSize : 1;
	uint8 animateUvOffset : 1;
	uint8 animateColorAdd : 1;
	uint8 animateColorMul : 1;
	uint8 animateColorMapLayer : 1;
	uint8 animateColorMap : 1;
	uint8 isEnabled : 1;
protected:
	uint16 _alignment0 = 0;
public:
	Color colorAdd = Color::transparent;
	Color colorMul = Color::white;
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};
	float colorMapLayer = 0.0f;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = "";
	#endif

	SpriteAnimFrame() noexcept : animateIsEnabled(false),  animateUvSize(false), 
		animateUvOffset(false), animateColorAdd(false), animateColorMul(false), 
		animateColorMapLayer(false), animateColorMap(false), isEnabled(true) { }

	bool hasAnimation() override
	{
		return animateIsEnabled | animateUvSize | animateUvOffset | animateColorAdd | 
			animateColorMul | animateColorMapLayer | animateColorMap;
	}

	half2& uvSize() noexcept { return *((half2*)&colorMap.unused); }
	half2 uvSize() const noexcept { return *((const half2*)&colorMap.unused); }
	half2& uvOffset() noexcept { return *((half2*)&descriptorSet.unused); }
	half2 uvOffset() const noexcept { return *((const half2*)&descriptorSet.unused); }
};

/***********************************************************************************************************************
 * @brief General sprite mesh rendering system.
 *
 * @details
 * Sprite is a 2D bitmap or animation that is integrated into a larger scene, acting as a single visual entity. 
 * Unlike 3D models composed of complex meshes, sprites are rendered as flat rectangular planes (quads) with a 
 * texture mapped onto them, frequently utilizing alpha transparency to define non-rectangular shapes.
 */
class SpriteRenderSystem : public InstanceRenderSystem, public ISerializable
{
public:
	struct PushConstants
	{
		uint32 instanceIndex;
		float colorMapLayer;
	};

	using SpriteFramePool = LinearPool<SpriteAnimFrame>;
protected:
	fs::path pipelinePath = "";
	string valueStringCache;

	/**
	 * @brief Creates a new sprite mesh render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	SpriteRenderSystem(const fs::path& pipelinePath) : pipelinePath(pipelinePath) { }

	void init() override;
	virtual void imageLoaded();

	static void resetComponent(View<Component> component);

	uint32 getReadyMeshesAsync(MeshRenderComponent* meshRenderView, 
		const f32x4& cameraPosition, const Frustum& frustum, f32x4x4& model) override;
	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 instanceIndex, int32 taskIndex) override;

	uint64 getBaseInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderView, void* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex);
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex);
	virtual DescriptorSet::Uniforms getSpriteUniforms(ID<ImageView> colorMap);
	ID<GraphicsPipeline> createBasePipeline() override;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, View<Component> component) override;

	static void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame);
	static void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame);
	static void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t);
	static void resetAnimation(View<AnimationFrame> frame);
public:
	/**
	 * @brief Returns sprite system animation frame pool.
	 */
	virtual SpriteFramePool& getSpriteFramePool() = 0;
	/**
	 * @brief Returns sprite system animation frame size in bytes.
	 */
	virtual psize getSpriteFrameSize() const = 0;

	/**
	 * @brief Creates shared base sprite descriptor set.
	 * 
	 * @param path sprite resource path
	 * @param colorMap sprite texture instance
	 */
	Ref<DescriptorSet> createSharedDS(string_view path, ID<Image> colorMap);
};

/***********************************************************************************************************************
 * @brief Base sprite mesh rendering system with components and animation frames.
 * @details See the @ref SpriteRenderSystem.
 *
 * @tparam C type of the system component
 * @tparam F type of the system animation frame
 *
 * @tparam DestroyComponents system should call destroy() function of the components
 * @tparam DestroyAnimationFrames system should call destroy() function of the animation frames
 */
template<class C = Component, class F = AnimationFrame, 
	bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class SpriteCompAnimSystem : public CompAnimSystem<C, F, 
	DestroyComponents, DestroyAnimationFrames>, public SpriteRenderSystem
{
protected:
	/**
	 * @brief Creates a new sprite mesh render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	SpriteCompAnimSystem(const fs::path& pipelinePath) : SpriteRenderSystem(pipelinePath) { }

	void resetComponent(View<Component> component, bool full) override
	{ SpriteRenderSystem::resetComponent(component); if (full) **View<C>(component) = C(); }

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override
	{ SpriteRenderSystem::serializeAnimation(serializer, frame); }
	void deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame) override
	{ SpriteRenderSystem::deserializeAnimation(deserializer, frame); }
	void animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t) override
	{ SpriteRenderSystem::animateAsync(component, a, b, t); }
	void resetAnimation(View<AnimationFrame> frame, bool full) override
	{ SpriteRenderSystem::resetAnimation(frame); if (full) **View<F>(frame) = F(); }

	MeshRenderPool& getMeshComponentPool() const override { return *((MeshRenderPool*)&this->components); }
	psize getMeshComponentSize() const override { return sizeof(C); }
	SpriteFramePool& getSpriteFramePool() override { return *((SpriteFramePool*)&this->animationFrames); }
	psize getSpriteFrameSize() const override { return sizeof(F); }
};

} // namespace garden
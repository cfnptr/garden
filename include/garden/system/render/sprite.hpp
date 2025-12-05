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
 * @brief Common sprite rendering functions.
 */

#pragma once
#include "garden/system/render/instance.hpp"

namespace garden
{

/**
 * @brief Sprite rendering data container.
 */
struct SpriteRenderComponent : public MeshRenderComponent
{
	f32x4 color = f32x4::one;              /**< Texture sRGB color multiplier. */
	Ref<Image> colorMap = {};              /**< Color map texture instance. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Descriptor set instance. */
	float2 uvSize = float2::one;           /**< Texture UV size. */
	float2 uvOffset = float2::zero;        /**< Texture UV offset. */
	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = "";            /**< Color map texture path. */
	float taskPriority = 0.0f;             /**< Texture load task priority. */
	#endif
	float colorMapLayer = 0.0f;            /**< Color map texture layer index. */
	bool isArray = false;                  /**< Is sprite texture type array. */
	bool useMipmap = false;                /**< Use sprite texture mipmap. */
};

/**
 * @brief Sprite animation frame container.
 */
struct SpriteAnimFrame : public AnimationFrame
{
protected:
	uint8 _alignment0 = 0;
public:
	uint16 animateIsEnabled : 1;
	uint16 animateColor : 1;
	uint16 animateUvSize : 1;
	uint16 animateUvOffset : 1;
	uint16 animateColorMapLayer : 1;
	uint16 animateColorMap : 1;
	uint16 isEnabled : 1;
	uint16 isArray : 1;
	uint16 useMipmap : 1;
	float2 uvSize = float2::one;
	float2 uvOffset = float2::zero;
	f32x4 color = f32x4::one;
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};
	float colorMapLayer = 0.0f;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = "";
	float taskPriority = 0.0f;
	#endif

	SpriteAnimFrame() : animateIsEnabled(false), animateColor(false), animateUvSize(false), animateUvOffset(false), 
		animateColorMapLayer(false), animateColorMap(false), isEnabled(true), isArray(false), useMipmap(false) { }

	bool hasAnimation() override
	{
		return animateIsEnabled | animateColor | animateUvSize | 
			animateUvOffset | animateColorMapLayer | animateColorMap;
	}
};

/***********************************************************************************************************************
 * @brief General sprite mesh rendering system.
 */
class SpriteRenderSystem : public InstanceRenderSystem, public ISerializable
{
public:
	struct BaseInstanceData
	{
		float4x4 mvp = float4x4::zero;
		float4 color = float4::zero;
		float2 uvSize = float2::zero;
		float2 uvOffset = float2::zero;
	};
	struct PushConstants
	{
		uint32 instanceIndex;
		float colorMapLayer;
	};

	using SpriteFramePool = LinearPool<SpriteAnimFrame>;
protected:
	fs::path pipelinePath = "";
	string valueStringCache;
	ID<ImageView> defaultImageView = {};

	/**
	 * @brief Creates a new sprite mesh render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	SpriteRenderSystem(const fs::path& pipelinePath);

	void init() override;
	void deinit() override;
	virtual void imageLoaded();

	static void resetComponent(View<Component> component);

	bool isMeshReadyAsync(MeshRenderComponent* meshRenderView) override;
	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 drawIndex, int32 taskIndex) override;

	uint64 getBaseInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
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
	/**
	 * @brief Destroys sprite mesh render system instance.
	 */
	~SpriteCompAnimSystem() override { }

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
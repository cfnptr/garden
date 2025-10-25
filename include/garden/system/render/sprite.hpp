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
#include "garden/animate.hpp"
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
 * @brief Sprite rendering system.
 */
class SpriteRenderSystem : public InstanceRenderSystem, public ISerializable, public IAnimatable
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
	ID<ImageView> defaultImageView = {};

	/**
	 * @brief Creates a new sprite render system instance.
	 * @param[in] pipelinePath target rendering pipeline path
	 */
	SpriteRenderSystem(const fs::path& pipelinePath);

	void init() override;
	void deinit() override;
	virtual void imageLoaded();

	void resetComponent(View<Component> component, bool full) override;
	void copyComponent(View<Component> source, View<Component> destination) override;

	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 drawIndex, int32 taskIndex) override;

	uint64 getBaseInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual DescriptorSet::Uniforms getSpriteUniforms(ID<ImageView> colorMap);
	ID<GraphicsPipeline> createBasePipeline() final;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, SpriteAnimFrame& frame);
	void resetAnimation(View<AnimationFrame> frame, bool full) override;
public:
	/**
	 * @brief Returns sprite animation frame pool.
	 */
	virtual SpriteFramePool& getAnimationFramePool() = 0;
	/**
	 * @brief Returns sprite animation frame size in bytes.
	 */
	virtual psize getAnimationFrameSize() const = 0;

	/**
	 * @brief Creates shared base sprite descriptor set.
	 * 
	 * @param path sprite resource path
	 * @param colorMap sprite texture instance
	 */
	Ref<DescriptorSet> createSharedDS(string_view path, ID<Image> colorMap);
};

/***********************************************************************************************************************
 * @brief Sprite mesh rendering component system.
 */
template<class C, class A, bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class SpriteRenderCompSystem : public SpriteRenderSystem
{
protected:
	LinearPool<C, DestroyComponents> components;
	LinearPool<A, DestroyAnimationFrames> animationFrames;

	SpriteRenderCompSystem(const fs::path& pipelinePath) : SpriteRenderSystem(pipelinePath) { }

	ID<Component> createComponent(ID<Entity> entity) override { return ID<Component>(components.create()); }
	void destroyComponent(ID<Component> instance) override
	{
		auto component = components.get(ID<C>(instance));
		resetComponent(View<Component>(component), false);
		components.destroy(ID<C>(instance));
	}

	string_view getComponentName() const override
	{
		static const string name = typeToString(typeid(C));
		return name;
	}
	type_index getComponentType() const override { return typeid(C); }
	View<Component> getComponent(ID<Component> instance) override
	{
		return View<Component>(components.get(ID<C>(instance)));
	}
	void disposeComponents() override
	{
		components.dispose();
		animationFrames.dispose();
	}

	MeshRenderPool& getMeshComponentPool() override
	{
		return *((MeshRenderPool*)&components);
	}
	psize getMeshComponentSize() const override { return sizeof(C); }

	SpriteFramePool& getAnimationFramePool() override
	{
		return *((SpriteFramePool*)&animationFrames);
	}
	psize getAnimationFrameSize() const override { return sizeof(A); }

	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) override
	{
		A frame;
		SpriteRenderSystem::deserializeAnimation(deserializer, frame);
		if (frame.hasAnimation())
			return ID<AnimationFrame>(animationFrames.create(frame));
		return {};
	}
	View<AnimationFrame> getAnimation(ID<AnimationFrame> instance) override
	{
		return View<AnimationFrame>(animationFrames.get(ID<A>(instance)));
	}
	void destroyAnimation(ID<AnimationFrame> instance) override
	{
		auto frame = animationFrames.get(ID<A>(instance));
		resetAnimation(View<AnimationFrame>(frame), false);
		animationFrames.destroy(ID<A>(instance));
	}
public:
	bool hasComponent(ID<Entity> entity) const
	{
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		return entityView->findComponent(typeid(C).hash_code());
	}
	View<C> getComponent(ID<Entity> entity) const
	{
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		auto componentData = entityView->findComponent(typeid(C).hash_code());
		if (!componentData)
		{
			throw EcsmError("Component is not added. ("
				"type: " + typeToString(typeid(C)) + ", "
				"entity:" + std::to_string(*entity) + ")");
		}
		return components.get(ID<C>(componentData->instance));
	}
	View<C> tryGetComponent(ID<Entity> entity) const
	{
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		auto componentData = entityView->findComponent(typeid(C).hash_code());
		if (!componentData)
			return {};
		return components.get(ID<C>(componentData->instance));
	}
	void resetComponentData(ID<Entity> entity, bool full = true)
	{
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		auto componentData = entityView->findComponent(typeid(C).hash_code());
		if (!componentData)
		{
			throw EcsmError("Component is not added. ("
				"type: " + typeToString(typeid(C)) + ", "
				"entity:" + std::to_string(*entity) + ")");
		}
		auto component = components.get(ID<C>(componentData->instance));
		resetComponent(View<Component>(component), full);
	}
};

} // namespace garden
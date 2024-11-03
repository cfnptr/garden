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
 * @brief Common sprite rendering functions.
 */

#pragma once
#include "garden/hash.hpp"
#include "garden/system/render/instance.hpp"

namespace garden
{

/**
 * @brief Sprite rendering data container.
 */
struct SpriteRenderComponent : public MeshRenderComponent
{
protected:
	uint8 _alignment = 0;
public:
	bool isArray = false;                  /**< Is sprite texture type array. */
	Ref<Image> colorMap = {};              /**< Color map texture instance. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Descriptor set instance. */
	float colorMapLayer = 0.0f;            /**< Color map texture layer index. */
	float4 colorFactor = float4(1.0f);     /**< Texture color multiplier. */
	float2 uvSize = float2(1.0f);          /**< Texture UV size. */
	float2 uvOffset = float2(0.0f);        /**< Texture UV offset. */

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = {};            /**< Color map texture path. */
	#endif
};

/**
 * @brief Sprite animation frame container.
 */
struct SpriteAnimationFrame : public AnimationFrame
{
	uint8 isEnabled : 1;
	uint8 animateIsEnabled : 1;
	uint8 animateColorFactor : 1;
	uint8 animateUvSize : 1;
	uint8 animateUvOffset : 1;
	uint8 animateColorMapLayer : 1;
	uint8 animateColorMap : 1;
	uint8 isArray : 1;
protected:
	uint16 _alignment0 = 0;
public:
	float4 colorFactor = float4(1.0f);
	float2 uvSize = float2(1.0f);
	float2 uvOffset = float2(0.0f);
	float colorMapLayer = 0.0f;
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = {};
	#endif

	SpriteAnimationFrame() : isEnabled(true), animateIsEnabled(false), animateColorFactor(false), animateUvSize(false),
		animateUvOffset(false), animateColorMapLayer(false), animateColorMap(false), isArray(false) { }
};

/***********************************************************************************************************************
 * @brief Sprite rendering system.
 */
class SpriteRenderSystem : public InstanceRenderSystem, public ISerializable, public IAnimatable
{
public:
	struct InstanceData
	{
		float4x4 mvp = float4x4(0.0f);
		float4 colorFactor = float4(0.0f);
		float4 sizeOffset = float4(0.0f);
	};
	struct PushConstants
	{
		uint32 instanceIndex;
		float colorMapLayer;
	};
protected:
	bool useDeferredBuffer : 1;
	bool useLinearFilter : 1;
	bool isTranslucent : 1;
	uint8 _alignment0 = 0;
	ID<ImageView> defaultImageView = {};
	fs::path pipelinePath = {};

	/**
	 * @brief Creates a new sprite render system instance.
	 * 
	 * @param[in] pipelinePath target rendering pipeline path
	 * @param useDeferredBuffer use deferred or forward rendering buffer
	 * @param useLinearFilter use linear or nearest texture filter
	 * @param isTranslucent is sprite using translucent rendering
	 */
	SpriteRenderSystem(const fs::path& pipelinePath, 
		bool useDeferredBuffer, bool useLinearFilter, bool isTranslucent);

	void init() override;
	void deinit() override;
	virtual void imageLoaded();

	void copyComponent(View<Component> source, View<Component> destination) override;

	bool isDrawReady() override;
	void drawAsync(MeshRenderComponent* meshRenderView, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 threadIndex) override;

	uint64 getInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 threadIndex);
	void setDescriptorSetRange(MeshRenderComponent* meshRenderView,
		DescriptorSet::Range* range, uint8& index, uint8 capacity) override;
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 threadIndex);
	virtual map<string, DescriptorSet::Uniform> getSpriteUniforms(ID<ImageView> colorMap);
	map<string, DescriptorSet::Uniform> getDefaultUniforms() override;
	ID<GraphicsPipeline> createPipeline() final;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, SpriteAnimationFrame& frame);
	static void destroyResources(View<SpriteAnimationFrame> frameView);

	static void destroyResources(View<SpriteRenderComponent> spriteRenderView);
public:
	/**
	 * @brief Returns sprite animation frame pool.
	 */
	virtual LinearPool<SpriteAnimationFrame>& getAnimationFramePool() = 0;
	/**
	 * @brief Returns sprite animation frame size in bytes.
	 */
	virtual psize getAnimationFrameSize() const = 0;

	/**
	 * @brief Creates shared sprite descriptor set.
	 * 
	 * @param path sprite resource path
	 * @param image sprite texture instance
	 */
	Ref<DescriptorSet> createSharedDS(const string& path, ID<Image> image);
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

	SpriteRenderCompSystem(const fs::path& pipelinePath, bool useDeferredBuffer, bool useLinearFilter, 
		bool isTranslucent) : SpriteRenderSystem(pipelinePath, useDeferredBuffer, useLinearFilter, isTranslucent) { }

	ID<Component> createComponent(ID<Entity> entity) override
	{
		return ID<Component>(components.create());
	}
	void destroyComponent(ID<Component> instance) override
	{
		auto componentView = components.get(ID<C>(instance));
		destroyResources(View<SpriteRenderComponent>(componentView));
		components.destroy(ID<C>(instance));
	}
	void copyComponent(View<Component> source, View<Component> destination) override
	{
		const auto sourceView = View<C>(source);
		auto destinationView = View<C>(destination);
		if constexpr (DestroyComponents)
			destinationView->destroy();
		**destinationView = **sourceView;
	}

	const string& getComponentName() const override
	{
		static const string name = typeToString(typeid(C));
		return name;
	}
	type_index getComponentType() const override
	{
		return typeid(C);
	}
	View<Component> getComponent(ID<Component> instance) override
	{
		return View<Component>(components.get(ID<C>(instance)));
	}
	void disposeComponents() override
	{
		components.dispose();
		animationFrames.dispose();
	}

	LinearPool<MeshRenderComponent>& getMeshComponentPool() override
	{
		return *((LinearPool<MeshRenderComponent>*)&components);
	}
	psize getMeshComponentSize() const override
	{
		return sizeof(C);
	}

	LinearPool<SpriteAnimationFrame>& getAnimationFramePool() override
	{
		return *((LinearPool<SpriteAnimationFrame>*)&animationFrames);
	}
	psize getAnimationFrameSize() const override
	{
		return sizeof(A);
	}
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) override
	{
		A frame;
		SpriteRenderSystem::deserializeAnimation(deserializer, frame);

		if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize ||
			frame.animateUvOffset || frame.animateColorMapLayer || frame.animateColorMap)
		{
			return ID<AnimationFrame>(animationFrames.create(frame));
		}

		return {};
	}
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) override
	{
		return View<AnimationFrame>(animationFrames.get(ID<A>(frame)));
	}
	void destroyAnimation(ID<AnimationFrame> frame) override
	{
		auto frameView = animationFrames.get(ID<A>(frame));
		destroyResources(View<SpriteAnimationFrame>(frameView));
		animationFrames.destroy(ID<A>(frame));
	}
public:
	bool hasComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		const auto& entityComponents = entityView->getComponents();
		return entityComponents.find(typeid(C)) != entityComponents.end();
	}
	View<C> getComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		const auto& pair = entityView->getComponents().at(typeid(C));
		return components.get(ID<C>(pair.second));
	}
	View<C> tryGetComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		const auto& entityComponents = entityView->getComponents();
		auto result = entityComponents.find(typeid(C));
		if (result == entityComponents.end())
			return {};
		return components.get(ID<C>(result->second.second));
	}
};

} // namespace garden
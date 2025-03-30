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
 * @brief Common model rendering functions.
 */

#pragma once
#include "garden/animate.hpp"
#include "garden/graphics/lod.hpp"
#include "garden/system/render/instance.hpp"

namespace garden
{

class ModelRenderSystem;

/**
 * @brief Model rendering data container.
 */
struct ModelRenderComponent : public MeshRenderComponent
{
protected:
	uint8 _alignment = 0;
	friend class ModelRenderSystem;
public:
	GraphicsPipeline::Index indexType = {}; /**< Index buffer indices type. */
	Ref<LodBuffer> lodBuffer = {};          /**< Model LOD buffer instance. */
	Ref<Image> colorMap = {};               /**< Color map texture instance. */
	Ref<Image> mraorMap = {};               /**< MRAOR map texture instance. */
	Ref<DescriptorSet> descriptorSet = {};  /**< Descriptor set instance. */
	f32x4 colorFactor = f32x4::one;         /**< Texture color multiplier. */
	uint32 indexCount = 0;                  /**< Model index buffer size. */

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path lodBufferPath = {};            /**< Model vertex buffer path. */
	fs::path colorMapPath = {};             /**< Color map texture path. */
	fs::path mraorMapPath = {};             /**< MRAOR map texture path. */
	#endif
};

/**
 * @brief Model animation frame container.
 */
struct ModelAnimationFrame : public AnimationFrame
{
	uint8 isEnabled : 1;
	uint8 animateIsEnabled : 1;
	uint8 animateColorFactor : 1;
	uint8 animateLodBuffer : 1;
	uint8 animateTextureMaps : 1;
protected:
	uint16 _alignment0 = 0;
public:
	f32x4 colorFactor = f32x4::one;
	Ref<LodBuffer> lodBuffer = {};
	Ref<Image> colorMap = {};
	Ref<Image> mraorMap = {};
	Ref<DescriptorSet> descriptorSet = {};

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path lodBufferPath = {};
	fs::path colorMapPath = {};
	fs::path mraorMapPath = {};
	#endif

	ModelAnimationFrame() : isEnabled(true), animateIsEnabled(false), 
		animateColorFactor(false), animateLodBuffer(false), animateTextureMaps(false) { }

	bool hasAnimation() override
	{
		return animateIsEnabled | animateColorFactor | animateLodBuffer | animateTextureMaps;
	}
};

/***********************************************************************************************************************
 * @brief Model rendering system.
 */
class ModelRenderSystem : public InstanceRenderSystem, public ISerializable, public IAnimatable
{
public:
	struct BaseInstanceData
	{
		float4x4 mvp = float4x4::zero;
		float4 colorFactor = float4::zero;
	};
	struct PushConstants
	{
		uint32 instanceIndex;
	};
protected:
	fs::path pipelinePath = {};
	ID<ImageView> defaultImageView = {};
	bool useNormalMapping = false;
	bool useDeferredBuffer = false;
	bool useLinearFilter = false;
	bool isTranslucent = false;

	/**
	 * @brief Creates a new model render system instance.
	 * 
	 * @param[in] pipelinePath target rendering pipeline path
	 * @param useNormalMapping load and use normal map textures
	 * @param useDeferredBuffer use deferred or forward rendering buffer
	 * @param useLinearFilter use linear or nearest texture filter
	 * @param isTranslucent is model using translucent rendering
	 */
	ModelRenderSystem(const fs::path& pipelinePath, bool useNormalMapping, 
		bool useDeferredBuffer, bool useLinearFilter, bool isTranslucent);

	void init() override;
	void deinit() override;

	virtual void imageLoaded();
	virtual void bufferLoaded();

	void copyComponent(View<Component> source, View<Component> destination) override;

	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 drawIndex, int32 taskIndex) override;

	uint64 getBaseInstanceDataSize() override;
	virtual void setInstanceData(ModelRenderComponent* modelRenderView, BaseInstanceData* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual void setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual map<string, DescriptorSet::Uniform> getModelUniforms(ID<ImageView> colorMap, ID<ImageView> mraorMap);
	ID<GraphicsPipeline> createBasePipeline() final;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, ModelAnimationFrame& frame);
	static void destroyResources(View<ModelAnimationFrame> frameView);

	static void destroyResources(View<ModelRenderComponent> spriteRenderView);
public:
	/**
	 * @brief Returns model animation frame pool.
	 */
	virtual LinearPool<ModelAnimationFrame>& getAnimationFramePool() = 0;
	/**
	 * @brief Returns model animation frame size in bytes.
	 */
	virtual psize getAnimationFrameSize() const = 0;

	/**
	 * @brief Creates shared base model descriptor set.
	 * 
	 * @param path model resource path
	 * @param image model texture instance
	 */
	Ref<DescriptorSet> createSharedDS(const string& path, ID<Image> colorMap,  ID<Image> mraorMap);
};

/***********************************************************************************************************************
 * @brief Model mesh rendering component system.
 */
template<class C, class A, bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class ModelRenderCompSystem : public ModelRenderSystem
{
protected:
	LinearPool<C, DestroyComponents> components;
	LinearPool<A, DestroyAnimationFrames> animationFrames;

	ModelRenderCompSystem(const fs::path& pipelinePath, bool useNormalMapping, 
		bool useDeferredBuffer, bool useLinearFilter, bool isTranslucent) : 
		ModelRenderSystem(pipelinePath, useNormalMapping, useDeferredBuffer, useLinearFilter, isTranslucent) { }

	ID<Component> createComponent(ID<Entity> entity) override
	{
		return ID<Component>(components.create());
	}
	void destroyComponent(ID<Component> instance) override
	{
		auto componentView = components.get(ID<C>(instance));
		destroyResources(View<ModelRenderComponent>(componentView));
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

	LinearPool<ModelAnimationFrame>& getAnimationFramePool() override
	{
		return *((LinearPool<ModelAnimationFrame>*)&animationFrames);
	}
	psize getAnimationFrameSize() const override
	{
		return sizeof(A);
	}
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) override
	{
		A frame;
		ModelRenderSystem::deserializeAnimation(deserializer, frame);
		if (frame.hasAnimation())
			return ID<AnimationFrame>(animationFrames.create(frame));
		return {};
	}
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) override
	{
		return View<AnimationFrame>(animationFrames.get(ID<A>(frame)));
	}
	void destroyAnimation(ID<AnimationFrame> frame) override
	{
		auto frameView = animationFrames.get(ID<A>(frame));
		destroyResources(View<ModelAnimationFrame>(frameView));
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
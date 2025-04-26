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

namespace garden
{

/**
 * @brief 9-slice sprite rendering data container.
 */
struct NineSliceRenderComponent : public SpriteRenderComponent
{
	float2 textureBorder = float2::zero;
	float2 windowBorder = float2::zero;
};

/**
 * @brief 9-slice sprite animation frame container.
 */
struct NineSliceAnimationFrame : public SpriteAnimationFrame
{
	float2 textureBorder = float2::zero;
	float2 windowBorder = float2::zero;
	bool animateTextureBorder = false;
	bool animateWindowBorder = false;

	bool hasAnimation() override
	{
		return SpriteAnimationFrame::hasAnimation() || 
			animateTextureBorder || animateWindowBorder;
	}
};

/***********************************************************************************************************************
 * @brief 9-slice sprite rendering system.
 */
class NineSliceRenderSystem : public SpriteRenderSystem
{
public:
	struct NineSliceInstanceData : public BaseInstanceData
	{
		f32x4 texWinBorder = f32x4::zero;
	};
protected:
	NineSliceRenderSystem(const fs::path& pipelinePath) : SpriteRenderSystem(pipelinePath) { }

	void copyComponent(View<Component> source, View<Component> destination) override;

	uint64 getBaseInstanceDataSize() override;
	void setInstanceData(SpriteRenderComponent* spriteRenderView, BaseInstanceData* instanceData,
		const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 threadIndex) override;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, NineSliceAnimationFrame& frame);
};

/***********************************************************************************************************************
 * @brief 9-slice sprite rendering component system.
 */
template<class C, class A, bool DestroyComponents = true, bool DestroyAnimationFrames = true>
class NineSliceRenderCompSystem : public NineSliceRenderSystem
{
protected:
	LinearPool<C, DestroyComponents> components;
	LinearPool<A, DestroyAnimationFrames> animationFrames;

	NineSliceRenderCompSystem(const fs::path& pipelinePath) : NineSliceRenderSystem(pipelinePath) { }

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

	string_view getComponentName() const override
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
		NineSliceRenderSystem::deserializeAnimation(deserializer, frame);
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
		destroyResources(View<SpriteAnimationFrame>(frameView));
		animationFrames.destroy(ID<A>(frame));
	}
public:
	bool hasComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		return entityView->findComponent(typeid(C).hash_code());
	}
	View<C> getComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		auto componentData = entityView->findComponent(typeid(C).hash_code());
		GARDEN_ASSERT(componentData);
		return components.get(ID<C>(componentData->instance));
	}
	View<C> tryGetComponent(ID<Entity> entity) const
	{
		assert(entity);
		const auto entityView = Manager::Instance::get()->getEntities().get(entity);
		auto componentData = entityView->findComponent(typeid(C).hash_code());
		if (!componentData)
			return {};
		return components.get(ID<C>(componentData->instance));
	}
};

} // namespace garden
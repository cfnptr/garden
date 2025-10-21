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

	void resetComponent(View<Component> component, bool full) override;
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

	LinearPool<MeshRenderComponent>& getMeshComponentPool() override
	{
		return *((LinearPool<MeshRenderComponent>*)&components);
	}
	psize getMeshComponentSize() const override { return sizeof(C); }

	LinearPool<SpriteAnimationFrame>& getAnimationFramePool() override
	{
		return *((LinearPool<SpriteAnimationFrame>*)&animationFrames);
	}
	psize getAnimationFrameSize() const override { return sizeof(A); }

	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) override
	{
		A frame;
		NineSliceRenderSystem::deserializeAnimation(deserializer, frame);
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
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
 * @brief World transformation functions.
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief Contains information about entity transformation within the 3D space and nodes.
 * @details Nodes describe ties (connections) between entities in the game world.
 */
struct TransformComponent final : public Component
{
private:
	ID<Entity> parent = {};
	uint64 uid = 0;
	f32x4 posChildCount = f32x4::zero;
	f32x4 scaleChildCap = f32x4(1.0f, 1.0f, 1.0f, 0.0f);
	quat rotation = quat::identity;
	ID<Entity>* childs = nullptr;
	bool selfActive = true;
	bool ancestorsActive = true;

	/**
	 * @brief Destroys childs array memory block, if allocated. 
	 */
	bool destroy();

	uint32& childCount() noexcept { return posChildCount.uints.w; }
	uint32& childCapacity() noexcept { return scaleChildCap.uints.w; }

	friend class TransformSystem;
	friend class LinearPool<TransformComponent>;
	friend class ComponentSystem<TransformComponent>;
public:
	bool modelWithAncestors = true; /**< Are ancestors accounted when calculating model matrix. */

	#if GARDEN_DEBUG || GARDEN_EDITOR
	string debugName; /**< Entity debug name. (Debug and editor only) */
	#endif

	/**
	 * @brief Returns entity position in the 3D space relative to the parent.
	 */
	f32x4 getPosition() const noexcept { return posChildCount; }
	/**
	 * @brief Sets entity position in the 3D space relative to the parent.
	 * @param position target entity position in 3D space
	 */
	void setPosition(f32x4 position) noexcept { posChildCount = f32x4(position, posChildCount.getW()); }
	/**
	 * @brief Sets entity position in the 3D space relative to the parent.
	 * @param position target entity position in 3D space
	 */
	void setPosition(float3 position) noexcept { posChildCount = f32x4(f32x4(position), posChildCount.getW()); }

	/**
	 * @brief Returns entity scale in the 3D space relative to the parent.
	 */
	f32x4 getScale() const noexcept { return scaleChildCap; }
	/**
	 * @brief Sets entity scale in the 3D space relative to the parent.
	 * @param scale target entity scale in 3D space
	 */
	void setScale(f32x4 scale) noexcept { scaleChildCap = f32x4(scale, scaleChildCap.getW()); }
	/**
	 * @brief Sets entity scale in the 3D space relative to the parent.
	 * @param scale target entity scale in 3D space
	 */
	void setScale(float3 scale) noexcept { scaleChildCap = f32x4(f32x4(scale), scaleChildCap.getW()); }

	/**
	 * @brief Returns entity rotation in the 3D space relative to the parent.
	 */
	quat getRotation() const noexcept { return rotation; }
	/**
	 * @brief Sets entity rotation in the 3D space relative to the parent.
	 * @param rotation target entity rotation in 3D space
	 */
	void setRotation(quat rotation) noexcept { this->rotation = rotation; }

	/**
	 * @brief Is this entity and its ancestors active.
	 * @details Is this entity should be processed and used by other systems.
	 */
	bool isActive() const noexcept { return selfActive && ancestorsActive; }
	/**
	 * @brief Is this entity self active.
	 * @details See the @ref isActive().
	 */
	bool isSelfActive() const noexcept { return selfActive; }
	/**
	 * @brief Are this entity ancestors active.
	 * @details See the @ref isActive().
	 */
	bool areAncestorsActive() const noexcept { return ancestorsActive; }

	/**
	 * @brief Sets this entity and it descendants active state.
	 * @details Is this entity should be processed and used by other systems.
	 * 
	 * @note
	 * If this entity has inactive ancestors it will still be in an inactive state.
	 * This is performance heavy operation if this entity has many descendants.
	 */
	void setActive(bool isActive) noexcept;

	/**
	 * @brief Returns this entity parent object, or null if it is root entity.
	 * @details Entity parent affects it transformation in the space.
	 */
	ID<Entity> getParent() const noexcept { return parent; }
	/**
	 * @brief Returns this entity children count.
	 * @details Use it to iterate over children array.
	 */
	uint32 getChildCount() const noexcept { return posChildCount.uints.w; }
	/**
	 * @brief Returns this entity children array capacity.
	 * @details Use it to shrink children array when it's too big.
	 */
	uint32 getChildCapacity() const noexcept { return scaleChildCap.uints.w; }
	/**
	 * @brief Returns this entity children array, or null if no children.
	 * @details Use it to iterate over entity children.
	 */
	const ID<Entity>* getChilds() const noexcept { return childs; }

	/**
	 * @brief Translates this entity by the specified translation.
	 * @param translation target entity translation
	 */
	void translate(f32x4 translation) noexcept
	{
		posChildCount = f32x4(posChildCount + translation, posChildCount.getW());
	}
	/**
	 * @brief Scales this entity by the specified scale.
	 * @param scale target entity scale
	 */
	void scale(f32x4 scale) noexcept
	{
		scaleChildCap = f32x4(scaleChildCap * scale, scaleChildCap.getW());
	}
	/**
	 * @brief Rotates this entity by the specified rotation.
	 * @param rotation target entity rotation
	 */
	void rotate(quat rotation) noexcept { this->rotation *= rotation; }

	/**
	 * @brief Calculates entity model matrix from it position, scale and rotation.
	 * @note It also takes into account parent and grandparents transforms.
	 * 
	 * @param cameraPosition rendering camera position or zero
	 * @return Entity model 4x4 float matrix.
	 */
	f32x4x4 calcModel(f32x4 cameraPosition = f32x4::zero) const noexcept;

	/*******************************************************************************************************************
	 * @brief Sets this entity parent object.
	 * @details You can pass null to unset the entity parent.
	 * @param parent target parent entity or null
	 */
	void setParent(ID<Entity> parent);

	/**
	 * @brief Adds a new child to this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 * @throw GardenError if child already has a parent.
	 */
	void addChild(ID<Entity> child);
	/**
	 * @brief Tries to add a new child to this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 * @return True if child has no parent and was added.
	 */
	bool tryAddChild(ID<Entity> child);

	/**
	 * @brief Does this entity have the specified child.
	 * @param index target child to check
	 */
	bool hasChild(ID<Entity> child) const noexcept;
	/**
	 * @brief Returns this entity child.
	 * @param index target child index in the array
	 */
	ID<Entity> getChild(uint32 index)
	{
		GARDEN_ASSERT(index < childCount());
		return childs[index];
	}

	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 * @throw GardenError if child not found.
	 */
	void removeChild(ID<Entity> child);
	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param index target child index in the array
	 * @throw GardenError if child not found.
	 */
	void removeChild(uint32 index)
	{
		GARDEN_ASSERT(index < childCount());
		removeChild(childs[index]);
	}

	/**
	 * @brief Tries to remove child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 * @return True if child is found and was removed.
	 */
	bool tryRemoveChild(ID<Entity> child);

	/**
	 * @brief Removes all children from this entity.
	 * @note It also changes parent of the children entities.
	 */
	void removeAllChilds();
	/**
	 * @brief Reduces childs array capacity to fit its size.
	 * @details Optimizes component memory consumption. 
	 */
	void shrinkChilds();

	/**
	 * @brief Does this entity have the specified ancestor.
	 * @details Including parent, grandparent, great-grandparent...
	 * @param ancestor target ancestor to check
	 */
	bool hasAncestor(ID<Entity> ancestor) const noexcept;
	/**
	 * @brief Does this entity have the specified descendant.
	 * @details Including child, grandchild, great-grandchild...
	 * @param descendant target ancestor to check
	 */
	bool hasDescendant(ID<Entity> descendant) const noexcept;
	/**
	 * @brief Does this entity or its descendants have static transform.
	 */
	bool hasStaticWithDescendants() const noexcept;
};

/**
 * @brief Transform animation frame container.
 */
struct TransformFrame final : public AnimationFrame
{
	bool animatePosition = false;
	bool animateScale = false;
	bool animateRotation = false;
	f32x4 position = f32x4::zero;
	f32x4 scale = f32x4::one;
	quat rotation = quat::identity;

	bool hasAnimation() final
	{
		return animatePosition || animateScale || animateRotation;
	}
};

/***********************************************************************************************************************
 * @brief Handles entity transformations in the 3D space.
 * 
 * @details
 * Fundamental aspect of the engine architecture that handles the 
 * positioning, rotation, scaling and other properties of objects within the 3D space.
 */
class TransformSystem final : public CompAnimSystem<TransformComponent, TransformFrame, true, false>, 
	public Singleton<TransformSystem>, public ISerializable
{
	using EntityParentPair = pair<ID<Entity>, uint64>;
	using EntityDuplicatePair = pair<ID<Entity>, ID<Entity>>;

	vector<ID<Entity>> entityStack;
	vector<EntityDuplicatePair> entityDuplicateStack;
	tsl::robin_map<uint64, ID<Entity>> deserializedEntities;
	vector<EntityParentPair> deserializedParents;
	string uidStringCache;

	#if GARDEN_DEBUG
	set<uint64> serializedEntities;
	#endif

	/**
	 * @brief Creates a new transformer system instance.
	 * @param setSingleton set system singleton instance
	 */
	TransformSystem(bool setSingleton = true);
	/**
	 * @brief Destroys transformer system instance.
	 */
	~TransformSystem() final;

	void destroyComponent(ID<Component> instance) final;
	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void postSerialize(ISerializer& serializer) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;
	void postDeserialize(IDeserializer& deserializer) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Destroys the entity and all it descendants.
	 * @param entity target entity to destroy
	 */
	void destroyRecursive(ID<Entity> entity);
	/**
	 * @brief Creates a duplicate of entity with all descendants.
	 * @param entity target entity to duplicate from
	 */
	ID<Entity> duplicateRecursive(ID<Entity> entity);
};

/***********************************************************************************************************************
 * @brief Component indicating that entity is static and it transform should't be changed.
 */
struct StaticTransformComponent final : public Component { };

/**
 * @brief Handles static components.
 */
class StaticTransformSystem final : public ComponentSystem<StaticTransformComponent, false>, 
	public Singleton<StaticTransformSystem>, public ISerializable
{
	/**
	 * @brief Creates a new static transform system instance.
	 * @param setSingleton set system singleton instance
	 */
	StaticTransformSystem(bool setSingleton = true);
	/**
	 * @brief Destroys static transform system instance.
	 */
	~StaticTransformSystem() final;

	string_view getComponentName() const final;
	friend class ecsm::Manager;
};

} // namespace garden
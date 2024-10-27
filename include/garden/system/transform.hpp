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
 * @brief World transformation functions.
 */

#pragma once
#include "garden/animate.hpp"

namespace garden
{

/**
 * @brief Contains information about object transformation within the 3D space and nodes.
 * @details Nodes describe ties (connections) between objects in the game world.
 */
struct TransformComponent final : public Component
{
	float3 position = float3(0.0f); /**< Object position in the 3D space relative to the parent. */
	float3 scale = float3(1.0f);    /**< Object scale in the 3D space relative to the parent. */
	quat rotation = quat::identity; /**< Object rotation in the 3D space relative to the parent. */
	#if GARDEN_DEBUG || GARDEN_EDITOR
	string debugName;               /**< Object debug name. (Debug and editor only) */
	#endif
private:
	ID<Entity>* childs = nullptr;
	uint64 uid = 0;
	ID<Entity> parent = {};
	uint32 childCount = 0;
	uint32 childCapacity = 0;
	bool selfActive = true;
	bool ancestorsActive = true;
	uint16 _alignment = 0;

	/**
	 * @brief Destroys childs array memory block, if allocated. 
	 */
	bool destroy();

	friend class TransformSystem;
	friend class LinearPool<TransformComponent>;
	friend class ComponentSystem<TransformComponent>;
public:
	/**
	 * @brief Returns true if this entity and it ancestors are active.
	 * @details Is this object should be processed and used by other systems.
	 */
	bool isActive() const noexcept { return selfActive && ancestorsActive; }
	/**
	 * @brief Returns true if this entity is self active.
	 * @details See the @ref isActive().
	 */
	bool isSelfActive() const noexcept { return selfActive; }
	/**
	 * @brief Returns true if this entity ancestors are active.
	 * @details See the @ref isActive().
	 */
	bool areAncestorsActive() const noexcept { return ancestorsActive; }

	/**
	 * @brief Sets this entity and it descendants active state.
	 * @details Is this object should be processed and used by other systems.
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
	uint32 getChildCount() const noexcept { return childCount; }
	/**
	 * @brief Returns this entity children array, or null if no children.
	 * @details Use it to iterate over entity children.
	 */
	const ID<Entity>* getChilds() const noexcept { return childs; }

	/**
	 * @brief Calculates entity model matrix from it position, scale and rotation.
	 * @note It also takes into account parent and grandparents transforms.
	 * 
	 * @param cameraPosition rendering camera position or zero
	 * @return Object model 4x4 float matrix.
	 */
	float4x4 calcModel(const float3& cameraPosition = float3(0.0f)) const noexcept;

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
	 * @throw runtime_error if child already has a parent.
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
	 * @brief Returns true if this entity has specified child.
	 * @param index target child to check
	 */
	bool hasChild(ID<Entity> child) const noexcept;
	/**
	 * @brief Returns this entity child.
	 * @param index target child index in the array
	 */
	ID<Entity> getChild(uint32 index)
	{
		GARDEN_ASSERT(index < childCount);
		return childs[index];
	}

	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 * @throw runtime_error if child not found.
	 */
	void removeChild(ID<Entity> child);
	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param index target child index in the array
	 * @throw runtime_error if child not found.
	 */
	void removeChild(uint32 index)
	{
		GARDEN_ASSERT(index < childCount);
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
	 * @brief Returns true if this entity has specified ancestor
	 * @details Including parent, grandparent, great-grandparent...
	 * @param ancestor target ancestor to check
	 */
	bool hasAncestor(ID<Entity> ancestor) const noexcept;
	/**
	 * @brief Returns true if this entity has specified descendant
	 * @details Including child, grandchild, great-grandchild...
	 * @param descendant target ancestor to check
	 */
	bool hasDescendant(ID<Entity> descendant) const noexcept;
	/**
	 * @brief Returns true if entity or it desscdendants has baked transform.
	 */
	bool hasBakedWithDescendants() const noexcept;
};

/**
 * @brief Transform animation frame container.
 */
struct TransformFrame final : public AnimationFrame
{
	bool animatePosition = false;
	bool animateScale = false;
	bool animateRotation = false;
	float3 position = float3(0.0f);
	float3 scale = float3(1.0f);
	quat rotation = quat::identity;
};

/***********************************************************************************************************************
 * @brief Handles object transformations in the 3D space.
 * 
 * @details
 * Fundamental aspect of the engine architecture that handles the 
 * positioning, rotation, scaling and other properties of objects within the 3D space.
 */
class TransformSystem final : public ComponentSystem<TransformComponent>, 
	public Singleton<TransformSystem>, public ISerializable, public IAnimatable
{
	using EntityParentPair = pair<ID<Entity>, uint64>;
	using EntityDuplicatePair = pair<ID<Entity>, ID<Entity>>;

	LinearPool<TransformFrame, false> animationFrames;
	vector<ID<Entity>> entityStack;
	vector<EntityDuplicatePair> entityDuplicateStack;
	map<uint64, ID<Entity>> deserializedEntities;
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
	 * @brief Destroy transformer system instance.
	 */
	~TransformSystem() final;

	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	void disposeComponents() final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void postSerialize(ISerializer& serializer) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	void postDeserialize(IDeserializer& deserializer) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns transform component pool.
	 */
	const LinearPool<TransformComponent>& getComponents() const noexcept { return components; }

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
 * @brief Component indicating that entity is baked and it transform should't be changed.
 */
struct BakedTransformComponent final : public Component { };

/**
 * @brief Handles baked, static components.
 */
class BakedTransformSystem final : public ComponentSystem<BakedTransformComponent, false>, 
	public Singleton<BakedTransformSystem>, public ISerializable
{
	/**
	 * @brief Creates a new baked transformer system instance.
	 * @param setSingleton set system singleton instance
	 */
	BakedTransformSystem(bool setSingleton = true);
	/**
	 * @brief Destroy baked transformer system instance.
	 */
	~BakedTransformSystem() final;

	const string& getComponentName() const final;
	friend class ecsm::Manager;
};

} // namespace garden
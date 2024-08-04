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

using namespace math;
using namespace ecsm;

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
	bool isActive = true;           /**< Is object should be processed and used by other systems. */
private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
	ID<Entity> parent = {};
	uint32 childCount = 0;
	uint32 childCapacity = 0;
	ID<Entity>* childs = nullptr;
	uint64 uid = 0;

	/**
	 * @brief Destroys childs array memory block, if allocated. 
	 */
	bool destroy();

	friend class TransformSystem;
	friend class LinearPool<TransformComponent>;
public:
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

	/**
	 * @brief Sets this entity parent object.
	 * @details You can pass null to unset the entity parent.
	 * @param parent target parent entity or null
	 */
	void setParent(ID<Entity> parent);
	/**
	 * @brief Adds a new child to this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 */
	void addChild(ID<Entity> child);
	/**
	 * @brief Returns true if this entity has specified child.
	 * @param index target child to check
	 */
	bool hasChild(ID<Entity> child) const noexcept;
	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param child target child entity
	 */
	void removeChild(ID<Entity> child);
	/**
	 * @brief Removes child from this entity.
	 * @note It also changes parent of the child entity.
	 * @param index target child index in the array
	 */
	void removeChild(uint32 index);
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
	 * @param index target ancestor to check
	 */
	bool hasAncestor(ID<Entity> ancestor) const noexcept;
	/**
	 * @brief Returns true if entity is active and has no unactive ancestors.
	 */
	bool isActiveWithAncestors() const noexcept;
	/**
	 * @brief Returns true if entity or it desscdendants has baked transform.
	 */
	bool hasBakedWithDescendants() const noexcept;
};

/**
 * @brief Contains information about transformation animation frame.
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
class TransformSystem final : public System, public ISerializable, public IAnimatable
{
	using EntityParentPair = pair<ID<Entity>, uint64>;
	using EntityDuplicatePair = pair<ID<Entity>, ID<Entity>>;

	LinearPool<TransformComponent> components;
	LinearPool<TransformFrame, false> animationFrames;
	vector<ID<Entity>> entityStack;
	vector<EntityDuplicatePair> entityDuplicateStack;
	map<uint64, ID<Entity>> deserializedEntities;
	vector<EntityParentPair> deserializedParents;
	string uidStringCache;
	#if GARDEN_DEBUG
	set<uint64> serializedEntities;
	#endif

	static TransformSystem* instance;

	/**
	 * @brief Creates a new transformer system instance.
	 */
	TransformSystem();
	/**
	 * @brief Destroy transformer system instance.
	 */
	~TransformSystem() final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	
	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
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

	/**
	 * @brief Returns true if entity has transform component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity transform component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<TransformComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity transform component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<TransformComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns transform system instance.
	 */
	static TransformSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

/***********************************************************************************************************************
 * @brief Component indicating that entity is baked and it transform should't be changed.
 */
struct BakedTransformComponent final : public Component { };

/**
 * @brief Handles baked, static components.
 */
class BakedTransformSystem final : public System, public ISerializable
{
protected:
	LinearPool<BakedTransformComponent, false> components;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
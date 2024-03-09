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
#include "garden/defines.hpp"
#include "garden/serialize.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;

/**
 * @brief Contains information about object transformation within the 3D space.
 */
struct TransformComponent final : public Component
{
	float3 position = float3(0.0f);
	float3 scale = float3(1.0f);
	quat rotation = quat::identity;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	string name;
	#endif
private:
	ID<Entity> parent = {};
	uint32 childCount = 0;
	uint32 childCapacity = 0;
	ID<Entity>* childs = nullptr;
	Manager* manager = nullptr;

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
	 * @return Object model 4x4 float matrix.
	 */
	float4x4 calcModel() const noexcept;

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
	 * @brief Returns true if this entity has specified child.
	 * @param index target child to check
	 */
	bool hasChild(ID<Entity> child) const noexcept;
	/**
	 * @brief Returns true if this entity has specified ancestor
	 * @details Including parent, grandparent, grandgrand...
	 * @param index target ancestor to check
	 */
	bool hasAncestor(ID<Entity> ancestor) const noexcept;
	/**
	 * @brief Returns true if this entity or it children has baked transform.
	 */
	bool hasBaked() const noexcept;
};

/***********************************************************************************************************************
 * @brief Handles object transformations in the 3D space.
 * 
 * @details
 * Fundamental aspect of the engine architecture that handles the 
 * positioning, rotation, scaling and other properties of objects within the 3D space.
 */
class TransformSystem final : public System, public ISerializable
{
public:
	using TransformComponents = LinearPool<TransformComponent>;
private:
	TransformComponents components;
	static TransformSystem* instance;

	/**
	 * @brief Creates a new transform system instance.
	 * @param[in,out] manager manager instance
	 */
	TransformSystem(Manager* manager);
	/**
	 * @brief Destroy transform system instance.
	 */
	~TransformSystem() final;

	#if GARDEN_EDITOR
	void preInit();
	void postDeinit();
	#endif

	const string& getComponentName() const final;
	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	
	void serialize(ISerializer& serializer, ID<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Component> component) final;
	
	static void destroyRecursive(Manager* manager, ID<Entity> entity);
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns transform component pool.
	 */
	const TransformComponents& getComponents() const noexcept { return components; }

	/**
	 * @brief Destroys the entity and all it descendants.
	 * @param entity target entity
	 */
	void destroyRecursive(ID<Entity> entity);

	/**
	 * @brief Returns transform system instance.
	 * @warning Do not use it if you have several transform system instances.
	 */
	static TransformSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // Transform system is not created.
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
class BakedTransformSystem final : public System
{
protected:
	LinearPool<BakedTransformComponent, false> components;

	/**
	 * @brief Creates a new baked transform system instance.
	 * @param[in,out] manager manager instance
	 */
	BakedTransformSystem(Manager* manager);

	const string& getComponentName() const final;
	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	friend class ecsm::Manager;
};

} // namespace garden
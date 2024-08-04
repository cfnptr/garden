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

#pragma once
#include "garden/serialize.hpp"
#include "garden/hash.hpp"
#include "ecsm.hpp"

/***********************************************************************************************************************
 * @file
 * @brief Common objects search functions.
 */

namespace garden
{

using namespace ecsm;
class LinkSystem;

/**
 * @brief Object universally unique identifier (UUID) and/or tag container.
 */
struct LinkComponent final : public Component
{
private:
	Hash128 uuid = {}; /**< Entity universally unique identifier (UUID) */
	string tag = {};   /**< Entity tag (Can be used by several entities) */

	bool destroy();

	friend class LinkSystem;
	friend class LinearPool<LinkComponent>;
public:
	/**
	 * @brief Returns entity universally unique identifier (UUID).
	 */
	const Hash128& getUUID() const noexcept { return uuid; }
	/**
	 * @brief Returns entity tag. (Can be used by several entities)
	 */
	const string& getTag() const noexcept { return tag; }

	/**
	 * @brief Generates and sets a new random UUID.
	 * @note Trying to get maximum randomness internally.
	 * @throw runtime_error if UUID collision has occured
	 */
	void regenerateUUID();

	/**
	 * @brief Sets entity UUID if it's not yet used.
	 * @param uuid target entity UUID
	 */
	bool trySetUUID(const Hash128& uuid);
	/**
	 * @brief Sets entity tag. (Can be used by several entities)
	 * @param tag target entity tag
	 */
	void setTag(const string& tag);
};

/***********************************************************************************************************************
 * @brief Handles fast objects search by unique identifier or tag.
 */
class LinkSystem final : public System, public ISerializable
{
	LinearPool<LinkComponent> components;
	map<Hash128, ID<LinkComponent>> uuidMap;
	multimap<string, ID<LinkComponent>> tagMap;
	string uuidStringCache;
	random_device randomDevice;

	static LinkSystem* instance;

	/**
	 * @brief Creates a new logging system instance.
	 */
	LinkSystem();
	/**
	 * @brief Destroys logging system instance.
	 */
	~LinkSystem() final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
	friend class LinkComponent;
public:
	/**
	 * @brief Returns link component pool.
	 */
	const LinearPool<LinkComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns link UUID map.
	 */
	const map<Hash128, ID<LinkComponent>>& getUuidMap() const noexcept { return uuidMap; }
	/**
	 * @brief Returns link tag map.
	 */
	const multimap<string, ID<LinkComponent>>& getTagMap() const noexcept { return tagMap; }
	/**
	 * @brief Returns link component view.
	 */
	View<LinkComponent> get(ID<LinkComponent> component) const noexcept { return components.get(component); }

	/**
	 * @brief Returns entity by UUID if found, otherwise null.
	 * @param[in] uuid target entity UUID
	 */
	ID<Entity> findEntity(const Hash128& uuid);

	/**
	 * @brief Returns entities iterator by tag if found.
	 * @param[in] tag target entities tag
	 */
	auto findEntities(const string& tag) { return tagMap.equal_range(tag); }
	/**
	 * @brief Returns entities array by tag if found.
	 * @param[in] tag target entities tag
	 */
	void findEntities(const string& tag, vector<ID<Entity>>& entities);

	/**
	 * @brief Returns true if entity has link component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity link component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<LinkComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity link component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<LinkComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns link system instance.
	 * @warning Do not use it if you have several link system instances.
	 */
	static LinkSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
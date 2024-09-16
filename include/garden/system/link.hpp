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
	friend class ComponentSystem<LinkComponent>;
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
class LinkSystem final : public ComponentSystem<LinkComponent>, public Singleton<LinkSystem>, public ISerializable
{
	map<Hash128, ID<Entity>> uuidMap;
	multimap<string, ID<Entity>> tagMap;
	string uuidStringCache;
	random_device randomDevice;

	/**
	 * @brief Creates a new logging system instance.
	 * @param setSingleton set system singleton instance
	 */
	LinkSystem(bool setSingleton = true);
	/**
	 * @brief Destroys logging system instance.
	 */
	~LinkSystem() final;

	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
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
	const map<Hash128, ID<Entity>>& getUuidMap() const noexcept { return uuidMap; }
	/**
	 * @brief Returns link tag map.
	 */
	const multimap<string, ID<Entity>>& getTagMap() const noexcept { return tagMap; }

	/**
	 * @brief Returns entity by UUID if found, otherwise null.
	 * @param[in] uuid target entity UUID
	 */
	ID<Entity> findEntity(const Hash128& uuid) const;

	/**
	 * @brief Returns entities iterator by tag if found.
	 * @param[in] tag target entities tag
	 */
	auto findEntities(const string& tag) const { return tagMap.equal_range(tag); }
	/**
	 * @brief Returns entities array by tag if found.
	 * @param[in] tag target entities tag
	 */
	void findEntities(const string& tag, vector<ID<Entity>>& entities) const;
};

} // namespace garden
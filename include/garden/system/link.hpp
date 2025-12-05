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

#pragma once
#include "garden/serialize.hpp"
#include "garden/hash.hpp"
#include "ecsm.hpp"

#include <map>

/***********************************************************************************************************************
 * @file
 * @brief Common entity search functions.
 */

namespace garden
{

class LinkSystem;

/**
 * @brief Entity universally unique identifier (UUID) and/or tag container.
 */
struct LinkComponent final : public Component
{
private:
	Hash128 uuid = {}; /**< Entity universally unique identifier (UUID) */
	string tag = {};   /**< Entity tag (Can be used by several entities) */

	friend class LinkSystem;
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
	 * @throw GardenError if UUID collision has occurred
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
	void setTag(string_view tag);
};

/***********************************************************************************************************************
 * @brief Handles fast entity search by unique identifier or tag.
 */
class LinkSystem final : public ComponentSystem<LinkComponent, false>, 
	public Singleton<LinkSystem>, public ISerializable
{
public:
	using UuidMap = tsl::robin_map<Hash128, ID<Entity>>;
	using TagMap = multimap<string, ID<Entity>, less<>>;
private:
	UuidMap uuidMap;
	TagMap tagMap;
	string valueStringCache;
	random_device randomDevice;

	/**
	 * @brief Creates a new link system instance.
	 * @param setSingleton set system singleton instance
	 */
	LinkSystem(bool setSingleton = true);
	/**
	 * @brief Destroys link system instance.
	 */
	~LinkSystem() final;

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;
	
	friend class ecsm::Manager;
	friend struct LinkComponent;
public:
	/**
	 * @brief Returns link UUID map.
	 */
	const UuidMap& getUuidMap() const noexcept { return uuidMap; }
	/**
	 * @brief Returns link tag map.
	 */
	const TagMap& getTagMap() const noexcept { return tagMap; }

	/**
	 * @brief Returns entity by UUID.
	 * @param[in] uuid target entity UUID
	 * @throw GardenError if entity UUID not found.
	 */
	ID<Entity> get(const Hash128& uuid) const
	{
		GARDEN_ASSERT(uuid);
		return uuidMap.at(uuid);
	}
	/**
	 * @brief Returns entity by UUID if found, otherwise null.
	 * @param[in] uuid target entity UUID
	 */
	ID<Entity> tryGet(const Hash128& uuid) const noexcept
	{
		GARDEN_ASSERT(uuid);
		auto searchResult = uuidMap.find(uuid);
		if (searchResult == uuidMap.end())
			return {};
		return searchResult->second;
	}

	/**
	 * @brief Returns entities iterator by tag if found.
	 * @param tag target entities tag string
	 */
	auto tryGet(string_view tag) const noexcept { return tagMap.equal_range(tag); }
	/**
	 * @brief Returns entities array by tag if found.
	 * @param tag target entities tag string
	 */
	void tryGet(string_view tag, vector<ID<Entity>>& entities) const
	{
		GARDEN_ASSERT(!tag.empty());
		auto result = tagMap.equal_range(tag);
		for (auto i = result.first; i != result.second; i++)
			entities.emplace_back(i->second);
	}

	/**
	 * @brief Returns first found entity by tag.
	 * @param tag target entity tag string
	 * @throw GardenError if entity tag not found.
	 */
	ID<Entity> getFirst(string_view tag) const
	{
		auto result = tagMap.find(tag);
		if (result == tagMap.end())
			throw GardenError("Entity tag not found.");
		return result->second;
	}
	/**
	 * @brief Returns first found entity by tag on success, otherwise null.
	 * @param tag target entity tag string
	 */
	ID<Entity> tryGetFirst(string_view tag) const noexcept
	{
		auto result = tagMap.find(tag);
		if (result == tagMap.end())
			return {};
		return result->second;
	}
};

} // namespace garden
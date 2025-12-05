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
 * @brief Common network communication functions.
 */

#pragma once
#include "garden/serialize.hpp"

namespace garden
{

class NetworkSystem;

/**
 * @brief Contains entity networking properties.
 */
struct NetworkComponent final : public Component
{
	bool isClientOwned = false; /**< Is this entity controlled by the client or server. */
private:
	char* clientUID = nullptr;
	uint32 entityUID = 0;

	friend class NetworkSystem;
public:
	float sendDelay = 0.0f; /**< Next data send delay time in seconds. */

	/**
	* @brief Returns entity controlling client unique identifier c-string.
	*/
	const char* getClientUID() const noexcept { return clientUID; }
	/**
	 * @brief Sets entity controlling client unique identifier.
	 * @param uid target client UID string
	 */
	void setClientUID(string_view uid);

	/**
	 * @brief Returns network entity unique identifier.
	 */
	uint32 getEntityUID() const noexcept { return entityUID; }
	/**
	 * @brief Sets network entity unique identifier.
	 * @return True if UID is was sent, false if UID is already exist.
	 * @param uid target network entity UID or 0
	 */
	bool trySetEntityUID(uint32 uid);
	/**
	 * @brief Sets network entity unique identifier.
	 * @return True if UID is was sent, false if UID is already exist.
	 * @param uid target network entity UID or 0
	 */
	void setEntityUID(uint32 uid)
	{
		if (!trySetEntityUID(uid))
			throw GardenError("Network entity UID already exist.");
	}
};

/***********************************************************************************************************************
 * @brief Handles server/client network communication.
 */
class NetworkSystem final : public ComponentSystem<NetworkComponent, false>, 
	public Singleton<NetworkSystem>, public ISerializable
{
	tsl::robin_map<uint32, ID<Entity>> entityMap;
	string valueStringCache;

	/**
	 * @brief Creates a new network system instance.
	 * @param setSingleton set system singleton instance
	 */
	NetworkSystem(bool setSingleton = true);
	/**
	 * @brief Destroys network system instance.
	 */
	~NetworkSystem() final;

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	friend class ecsm::Manager;
	friend class NetworkComponent;
public:
	/**
	 * @brief Returns network entity by UID if found.
	 * @param uid target network entity UID
	 */
	ID<Entity> findEntity(uint32 uid) const noexcept
	{
		GARDEN_ASSERT(uid != 0);
		auto result = entityMap.find(uid);
		if (result == entityMap.end())
			return {};
		return result.value();
	}
};

} // namespace garden
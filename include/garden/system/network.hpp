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

/**
 * @brief Contains entity networking properties.
 */
struct NetworkComponent final : public Component
{
	bool isClientOwned = false; /**< Is this entity controlled by the client or server. */
};

/***********************************************************************************************************************
 * @brief Handles server/client network communication.
 */
class NetworkSystem final : public ComponentSystem<NetworkComponent, false>, 
	public Singleton<NetworkSystem>, public ISerializable
{
	/**
	 * @brief Creates a new network system instance.
	 * @param setSingleton set system singleton instance
	 */
	NetworkSystem(bool setSingleton = true);
	/**
	 * @brief Destroys network system instance.
	 */
	~NetworkSystem() final;

	void preInit();
	void postDeinit();

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;
	
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	friend class ecsm::Manager;
};

} // namespace garden
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
 * @brief Common Valve Steam API functions.
 */

#pragma once
#include "garden/system/locale.hpp"

#if GARDEN_STEAMWORKS_SDK
namespace garden
{

using namespace ecsm;
class SteamEventHandler;

/**
 * @brief Handles Valve Steam API functions.
 */
class SteamApiSystem final : public System, public Singleton<SteamApiSystem>
{
public:
	using OnAuthTicket = std::function<void(const uint8*, int)>;
private:
	void* eventHandler = nullptr;
	OnAuthTicket onAuthTicket = nullptr;
	uint32 authTicket = 0;

	/**
	 * @brief Creates a new Valve Steam API system instance.
	 * @param setSingleton set system singleton instance
	 */
	SteamApiSystem(bool setSingleton = true);
	/**
	 * @brief Destroys Valve Steam API system instance.
	 */
	~SteamApiSystem() final;

	void update();

	friend class ecsm::Manager;
	friend class garden::SteamEventHandler;
public:
	/**
	 * @brief Returns current game language that the user has set.
	 */
	Language getGameLanguage() const noexcept;

	/**
	 * @brief Requests steam client web API authentication ticket.
	 *
	 * @param[in] onAuthTicket on web API auth ticket generate callback
	 * @param[in] identity optional parameter to identify the service the ticket will be sent to
	 */
	bool getAuthTicketWebAPI(const OnAuthTicket& onAuthTicket, const char* identity = nullptr);
	/**
	 * @brief Cancels steam client authentication ticket.
	 */
	void cancelAuthTicket();
};

} // namespace garden
#endif
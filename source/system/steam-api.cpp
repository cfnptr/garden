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

#include "garden/system/steam-api.hpp"

#if GARDEN_STEAMWORKS_SDK
#include "steam/steam_api.h"

using namespace garden;

//**********************************************************************************************************************
SteamApiSystem::SteamApiSystem(bool setSingleton) : Singleton(setSingleton)
{
	if (SteamAPI_RestartAppIfNecessary(GARDEN_STEAMWORKS_APP_ID))
		exit(1);
	if (!SteamAPI_Init())
		throw GardenError("Failed to initialize Steam API.");

	ECSM_SUBSCRIBE_TO_EVENT("Update", SteamApiSystem::update);
}
SteamApiSystem::~SteamApiSystem()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", SteamApiSystem::update);

	SteamAPI_Shutdown();
	unsetSingleton();
}

void SteamApiSystem::update()
{
	SteamAPI_RunCallbacks();
}
#endif
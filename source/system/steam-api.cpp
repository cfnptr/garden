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
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"
#include "steam/steam_api.h"

using namespace garden;

namespace garden
{

class SteamEventHandler final
{
	SteamApiSystem* steamApiSystem = nullptr;
	
	STEAM_CALLBACK(SteamEventHandler, GetTicketForWebApiResponse, GetTicketForWebApiResponse_t);
public:
	SteamEventHandler(SteamApiSystem* steamApiSystem) noexcept : steamApiSystem(steamApiSystem) { }
};

}; // namespace garden

void SteamEventHandler::GetTicketForWebApiResponse(GetTicketForWebApiResponse_t* callback)
{
	if (steamApiSystem->authTicket != callback->m_hAuthTicket)
	{
		if (callback->m_eResult == k_EResultOK)
			SteamUser()->CancelAuthTicket(callback->m_hAuthTicket);
		GARDEN_LOG_WARN("Canceled unused Steam web API auth ticket.");
		return;
	}

	if (callback->m_eResult != k_EResultOK)
	{
		GARDEN_LOG_ERROR("Failed to get Steam web API auth ticket. (result: " + to_string(callback->m_eResult));
		steamApiSystem->onAuthTicket(nullptr, 0);
		return;
	}

	GARDEN_LOG_DEBUG("Got Steam web API auth ticket.");
	steamApiSystem->onAuthTicket(callback->m_rgubTicket, callback->m_cubTicket);
	steamApiSystem->onAuthTicket = nullptr;
}

//**********************************************************************************************************************
SteamApiSystem::SteamApiSystem(bool setSingleton) : Singleton(setSingleton)
{
	if (SteamAPI_RestartAppIfNecessary(GARDEN_STEAMWORKS_APP_ID))
		exit(1);
	if (!SteamAPI_Init())
		throw GardenError("Failed to initialize Steam API.");

	authTicket = k_HAuthTicketInvalid;
	eventHandler = new SteamEventHandler(this);

	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Update", SteamApiSystem::update);
}
SteamApiSystem::~SteamApiSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", SteamApiSystem::update);
	}

	delete (SteamEventHandler*)eventHandler;
	SteamAPI_Shutdown();
	unsetSingleton();
}

void SteamApiSystem::update()
{
	SET_CPU_ZONE_SCOPED("Steam API Update");
	SteamAPI_RunCallbacks();
}

Language SteamApiSystem::getGameLanguage() const noexcept
{
	auto language = string_view(SteamApps()->GetCurrentGameLanguage());
	if (language == "arabic") return Language::Arabic;
	if (language == "bulgarian") return Language::Bulgarian;
	if (language == "schinese") return Language::ChineseSimpl;
	if (language == "tchinese") return Language::ChineseTrad;
	if (language == "czech") return Language::Czech;
	if (language == "danish") return Language::Danish;
	if (language == "dutch") return Language::Dutch;
	if (language == "finnish") return Language::Finnish;
	if (language == "french") return Language::French;
	if (language == "german") return Language::German;
	if (language == "greek") return Language::Greek;
	if (language == "hungarian") return Language::Hungarian;
	if (language == "indonesian") return Language::Indonesian;
	if (language == "italian") return Language::Italian;
	if (language == "japanese") return Language::Japanese;
	if (language == "german") return Language::German;
	if (language == "koreana") return Language::Korean;
	if (language == "norwegian") return Language::Norwegian;
	if (language == "polish") return Language::Polish;
	if (language == "portuguese" || language == "brazilian") return Language::Portuguese;
	if (language == "romanian") return Language::Romanian;
	if (language == "russian") return Language::Russian;
	if (language == "spanish" || language == "latam") return Language::Spanish;
	if (language == "swedish") return Language::Swedish;
	if (language == "thai") return Language::Thai;
	if (language == "turkish") return Language::Turkish;
	if (language == "ukrainian") return Language::Ukrainian;
	if (language == "vietnamese") return Language::Vietnamese;
	return Language::English;
}

bool SteamApiSystem::getAuthTicketWebAPI(const OnAuthTicket& onAuthTicket, const char* identity)
{
	this->onAuthTicket = onAuthTicket;
	authTicket = SteamUser()->GetAuthTicketForWebApi(identity);
	if (authTicket != k_HAuthTicketInvalid)
		return true;
	GARDEN_LOG_ERROR("Failed to request Steam web API auth ticket.");
	return false;
}
void SteamApiSystem::cancelAuthTicket()
{
	if (authTicket == k_HAuthTicketInvalid)
		return;

	SteamUser()->CancelAuthTicket(authTicket);
	authTicket = k_HAuthTicketInvalid;
	GARDEN_LOG_DEBUG("Canceled Steam web API auth ticket.");
}
#endif
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

#include "garden/system/locale.hpp"
#include "garden/system/steam-api.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/log.hpp"
#include <sstream>

using namespace garden;

static bool loadLocaleStrings(LocaleSystem::StringMap& strings, string_view module, Language language)
{
	strings = {};

	vector<uint8> localeData; auto localePath = fs::path("locales");
	if (!module.empty()) localePath /= module;
	localePath /= toCodeString(language); localePath.replace_extension(".txt");

	if (!ResourceSystem::Instance::get()->loadData(localePath, localeData))
		return false;

	try
	{
		localeData.resize(localeData.size() + 1);
		auto data = localeData.data(); data[localeData.size() - 1] = '\0';
		istringstream stream((const char*)data); localeData = {};

		string line;
		while (std::getline(stream, line))
		{
			auto offset = line.find(':');
			if (offset == string::npos || offset == 0 || line.length() - offset < 3)
			{
				GARDEN_LOG_DEBUG("Invalid locale string! (path: " + 
					localePath.generic_string() + ", line: " + line + ")");
				continue;
			}

			auto key = string(line.c_str(), offset);
			offset++;
			if (line[offset + 1] == ' ') offset++;

			if (line.length() - offset == 0)
			{
				GARDEN_LOG_DEBUG("Invalid locale string! (path: " + 
					localePath.generic_string() + ", line: " + line + ")");
				continue;
			}

			auto value = string(line.c_str() + offset, line.length() - offset);
			auto result = strings.emplace(std::move(key), std::move(value));

			if (!result.second)
			{
				GARDEN_LOG_DEBUG("Duplicate locale string! (path: " + 
					localePath.generic_string() + ", line: " + line + ")");
			}
		}
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load localization strings. (error: " + string(e.what()) + ")");
		return false;
	}
	return true;
}

//**********************************************************************************************************************
LocaleSystem::LocaleSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", LocaleSystem::preInit);
}
LocaleSystem::~LocaleSystem()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", LocaleSystem::preInit);
	unsetSingleton();
}

void LocaleSystem::preInit()
{
	auto isLanguageSet = false;

	auto settingsSystem = SettingsSystem::Instance::tryGet();
	if (settingsSystem)
	{
		string languageCode = "-";
		settingsSystem->getString("locale.language", languageCode);

		if (languageCode != "-")
			isLanguageSet = getCodeLanguage(languageCode, loadedLanguage);
	}

	#if GARDEN_STEAMWORKS_SDK
	if (!isLanguageSet)
	{
		auto steamApiSystem = SteamApiSystem::Instance::tryGet();
		if (steamApiSystem)
			loadedLanguage = steamApiSystem->getGameLanguage();
	}
	#endif

	loadLocaleStrings(generalStrings, "", loadedLanguage);
}

void LocaleSystem::setLanguage(Language language)
{
	if (loadedLanguage == language)
		return;

	loadLocaleStrings(generalStrings, "", language);
	for (auto i = modules.begin(); i != modules.end(); i++)
		loadLocaleStrings(i.value(), i->first, language);

	loadedLanguage = language;
	GARDEN_LOG_INFO("Loaded localization strings: " + string(toCodeString(language)));
}

bool LocaleSystem::loadModule(string_view module)
{
	if (modules.find(module) != modules.end())
		return false;

	StringMap moduleStrings;
	if (!loadLocaleStrings(moduleStrings, module, loadedLanguage))
		return false;

	auto emplaceResult = modules.emplace(module, std::move(moduleStrings));
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return true;
}
bool LocaleSystem::unloadModule(string_view module)
{
	auto result = modules.find(module);
	if (result == modules.end())
		return false;

	modules.erase(result);
	return true;
}
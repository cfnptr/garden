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
	localePath /= toString(language); localePath.replace_extension(".txt");

	if (!ResourceSystem::Instance::get()->loadData(localePath, localeData))
		return false;

	istringstream stream(string((const char*)
		localeData.data(), localeData.size()));
	localeData = {};

	string line;
	while (std::getline(stream, line))
	{
		if (line.empty())
			continue;

		auto offset = line.find(':');
		if (offset == string::npos || offset == 0 || line.length() - offset < 3)
		{
			GARDEN_LOG_DEBUG("Invalid locale string! (path: " + 
				localePath.generic_string() + ", line: " + line + ")");
			continue;
		}

		auto key = string(line.c_str(), offset);
		offset++;
		if (line[offset] == ' ') offset++;

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
	return true;
}

//**********************************************************************************************************************
LocaleSystem::LocaleSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("LocaleChange");
	ECSM_SUBSCRIBE_TO_EVENT("PreInit", LocaleSystem::preInit);
}
LocaleSystem::~LocaleSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", LocaleSystem::preInit);
		manager->unregisterEvent("LocaleChange");
	}
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

	bigFontSize = ::isBigFontSize(loadedLanguage);

	if (!loadLocaleStrings(generalStrings, "", loadedLanguage))
	{
		loadLocaleStrings(generalStrings, "", Language::English);
		bigFontSize = false;
		return;
	}

	GARDEN_LOG_INFO("Loaded localization language: " + string(toString(loadedLanguage)));
}

void LocaleSystem::setLanguage(Language language)
{
	if (loadedLanguage == language)
		return;

	bigFontSize = ::isBigFontSize(language);

	if (!loadLocaleStrings(generalStrings, "", language))
	{
		loadLocaleStrings(generalStrings, "", Language::English);
		bigFontSize = false;
	}

	for (auto i = modules.begin(); i != modules.end(); i++)
	{
		if (!loadLocaleStrings(i.value(), i->first, language))
			loadLocaleStrings(i.value(), i->first, Language::English);
	}

	loadedLanguage = language;
	GARDEN_LOG_INFO("Changed localization language: " + string(toString(language)));
	Manager::Instance::get()->runEvent("LocaleChange");
}

string_view LocaleSystem::get(string_view key, bool andModules) const
{
	GARDEN_ASSERT(!key.empty());
	auto result = generalStrings.find(key);
	if (result != generalStrings.end())
		return result->second;

	if (andModules)
	{
		for (auto& module : modules)
		{
			result = module.second.find(key);
			if (result != module.second.end())
				return result->second;
		}
	}

	GARDEN_LOG_ERROR("Missing string localization. (key: " + string(key) + 
		", language: " + string(toString(loadedLanguage)) + ")");
	return key;
}

bool LocaleSystem::loadModule(string_view module)
{
	if (modules.find(module) != modules.end())
		return false;

	StringMap moduleStrings;
	if (!loadLocaleStrings(moduleStrings, module, loadedLanguage))
	{
		if (!loadLocaleStrings(moduleStrings, module, Language::English))
			return false;
	}

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
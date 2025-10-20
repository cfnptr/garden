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
#include "garden/system/resource.hpp"
#include "garden/system/log.hpp"
#include <sstream>

using namespace garden;

static void loadLocaleStrings(tsl::robin_map<string, string, ecsm::SvHash, ecsm::SvEqual>& strings, Language language)
{
	strings = {};

	vector<uint8> localeData; auto localePath = fs::path("locale");
	localePath /= toCodeString(language); localePath.replace_extension(".txt");
	if (!ResourceSystem::Instance::get()->loadData(localePath, localeData))
		return;

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
				continue;

			auto key = string(line.c_str(), offset);
			offset++;
			if (line[offset + 1] == ' ') offset++;

			if (line.length() - offset == 0)
				continue;

			auto value = string(line.c_str() + offset, line.length() - offset);
			strings.emplace(std::move(key), std::move(value));
		}
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load localization strings. (error: " + string(e.what()) + ")");
	}
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
	// TODO: get current language from steam system.

	loadLocaleStrings(localeStrings, loadedLanguage);
}

void LocaleSystem::setLanguage(Language language)
{
	if (loadedLanguage == language)
		return;

	loadLocaleStrings(localeStrings, language);
	loadedLanguage = language;
	GARDEN_LOG_INFO("Loaded localization strings: " + string(toCodeString(language)));
}
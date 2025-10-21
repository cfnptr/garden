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
#include "garden/defines.hpp"
#include "tsl/robin_map.h"
#include "ecsm.hpp"

/***********************************************************************************************************************
 * @file
 * @brief Common language localization (translation) functions.
 */

namespace garden
{

using namespace ecsm;

/**
 * @brief Most commonly used content languages.
 * @details https://en.wikipedia.org/wiki/Languages_used_on_the_Internet
 */
enum class Language : uint8
{
	English, Spanish, German, Japanese, French, Portuguese, Russian, Italian, Dutch, Polish, Turkish, Chinese, Persian,
	Vietnamese, Indonesian, Czech, Korean, Ukrainian, Hungarian, Swedish, Arabic, Romanian, Greek, Danish, Finnish,
	Hebrew, Slovak, Thai, Bulgarian, Croatian, Norwegian, Lithuanian, Serbian, Slovenian, Catalan, Estonian, Latvian,
	Bosnian, Hindi, Azerbaijani, Georgian, Icelandic, Kazakh, Macedonian, Bengali, Albanian, Malay, Uzbek, Armenian, 
	Urdu, Count
};
/**
 * @brief Spoken languages ISO 639 code strings.
 */
constexpr const char* languageCodes[(psize)Language::Count] =
{
	"en", "es", "de", "ja", "fr", "pt", "ru", "it", "nl", "pl", "tr", "zh", "fa", "vi", "id", "cs", "ko", "uk", "hu",
	"sv", "ar", "ro", "el", "da", "fi", "he", "sk", "th", "bg", "hr", "no", "lt", "sr", "sl", "ca", "et", "lv", "bs",
	"hi", "az", "ka", "is", "kk", "mk", "bn", "sq", "ms", "uz", "hy", "ur"
};

/**
 * @brief Returns languages ISO 639 code string.
 * @param language target language
 */
static string_view toCodeString(Language language) noexcept
{
	GARDEN_ASSERT(language < Language::Count);
	return languageCodes[(psize)language];
}
/**
 * @brief Tries to convert ISO 639 code string to language.
 * 
 * @param codeString target ISO 639 code string
 * @param[out] language converted language
 */
static bool getCodeLanguage(string_view codeString, Language& language) noexcept
{
	for (uint8 i = 0; i < (uint8)Language::Count; i++)
	{
		if (languageCodes[i] != codeString)
			continue;
		language = (Language)i;
		return true;
	}
	return false;
}

/***********************************************************************************************************************
 * @brief Handles string localization (translation) for different languages.
 */
class LocaleSystem final : public System, public Singleton<LocaleSystem>
{
public:
	using StringMap = tsl::robin_map<string, string, SvHash, SvEqual>;
private:
	StringMap generalStrings;
	tsl::robin_map<string, StringMap, SvHash, SvEqual> modules;
	Language loadedLanguage = Language::English;

	/**
	 * @brief Creates a new locale system instance.
	 * @param setSingleton set system singleton instance
	 */
	LocaleSystem(bool setSingleton = true);
	/**
	 * @brief Destroys locale system instance.
	 */
	~LocaleSystem() final;

	void preInit();

	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns loaded localizaion strings language.
	 */
	Language getLanguage() const noexcept { return loadedLanguage; }
	/**
	 * @brief Loads specified language localization strings.
	 * @param language target language to use
	 */
	void setLanguage(Language language);

	/**
	 * @brief Returns localized (translated) string.
	 * @param name target string name
	 */
	string_view get(string_view name) const noexcept
	{
		GARDEN_ASSERT(!name.empty());
		auto result = generalStrings.find(name);
		if (result == generalStrings.end())
			return name;
		return result->second;
	}
	/**
	 * @brief Returns module localized (translated) string.
	 *
	 * @param module localization module name
	 * @param name target string name
	 */
	string_view get(string_view module, string_view name) const noexcept
	{
		GARDEN_ASSERT(!module.empty());
		GARDEN_ASSERT(!name.empty());
		auto moduleResult = modules.find(module);
		if (moduleResult == modules.end())
			return name;
		auto stringResult = moduleResult->second.find(name);
		if (stringResult == moduleResult->second.end())
			return name;
		return stringResult->second;
	}

	/**
	 * @brief Returns true if localization module is loaded.
	 * @param module target localization module name
	 */
	bool isModuleLoaded(string_view module) const noexcept { return modules.find(module) != modules.end(); }
	/**
	 * @brief Loads localization module strings.
	 * @param module target localization module name
	 */
	bool loadModule(string_view module);
	/**
	 * @brief Unloads localization module strings.
	 * @param module target localization module name
	 */
	bool unloadModule(string_view module);
};

} // namespace garden
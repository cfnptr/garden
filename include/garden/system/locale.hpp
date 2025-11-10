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
#include "garden/utf.hpp"
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
	English, Spanish, German, Japanese, French, Portuguese, Russian, Italian, Dutch, Polish, Turkish, ChineseTrad,
	ChineseSimpl, Persian, Vietnamese, Indonesian, Czech, Korean, Ukrainian, Hungarian, Swedish, Arabic, Romanian, 
	Greek, Danish, Finnish, Hebrew, Slovak, Thai, Bulgarian, Croatian, Norwegian, Lithuanian, Serbian, Slovenian, 
	Catalan, Estonian, Latvian, Bosnian, Hindi, Azerbaijani, Georgian, Icelandic, Kazakh, Macedonian, Bengali, 
	Albanian, Malay, Uzbek, Armenian, Urdu, Count
};
/**
 * @brief Spoken language name strings.
 */
constexpr const char* languageNames[(psize)Language::Count] =
{
	"english", "spanish", "german", "japanese", "french", "portuguese", "russian", "italian", "dutch", "polish", 
	"turkish", "tchinese", "schinese", "persian", "vietnamese", "indonesian", "czech", "korean", "ukrainian", 
	"hungarian", "swedish", "arabic", "romanian", "greek", "danish", "finnish", "hebrew", "slovak", "thai", 
	"bulgarian", "croatian", "norwegian", "lithuanian", "serbian", "slovenian", "catalan", "estonian", "latvian", 
	"bosnian", "hindi", "azerbaijani", "georgian", "icelandic", "kazakh", "macedonian", "bengali", "albanian", 
	"malay", "uzbek", "armenian", "urdu"
};

/**
 * @brief Returns languages name string.
 * @param language target language
 */
static string_view toString(Language language) noexcept
{
	GARDEN_ASSERT(language < Language::Count);
	return languageNames[(psize)language];
}
/**
 * @brief Tries to convert name string to language.
 * 
 * @param name target language name string
 * @param[out] language converted language
 */
static bool getCodeLanguage(string_view name, Language& language) noexcept
{
	for (uint8 i = 0; i < (uint8)Language::Count; i++)
	{
		if (languageNames[i] != name)
			continue;
		language = (Language)i;
		return true;
	}
	return false;
}

/**
 * @brief Returns true if specified language requires increased font size.
 * @param language target spoken language
 */
static bool isBigFontSize(Language language) noexcept
{
	switch (language)
	{
	case Language::Japanese: case Language::ChineseTrad: case Language::ChineseSimpl: case Language::Korean:
	case Language::Persian: case Language::Arabic: case Language::Hebrew: case Language::Thai:
	case Language::Hindi: case Language::Bengali: case Language::Urdu:
		return true;
	default: return false;
	}
}

/***********************************************************************************************************************
 * @brief Handles string localization (translation) for different languages.
 */
class LocaleSystem final : public System, public Singleton<LocaleSystem>
{
public:
	using StringMap = tsl::robin_map<string, string, SvHash, SvEqual>;
	using ModuleMap = tsl::robin_map<string, StringMap, SvHash, SvEqual>;
private:
	StringMap generalStrings;
	ModuleMap modules;
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
	 * @brief Returns general localization string map.
	 */
	const StringMap& getGeneralStrings() const noexcept { return generalStrings; }
	/**
	 * @brief Returns localization module map.
	 */
	const ModuleMap& getModules() const noexcept { return modules; }

	/**
	 * @brief Returns loaded localizaion strings language.
	 */
	Language getLanguage() const noexcept { return loadedLanguage; }
	/**
	 * @brief Loads specified language localization strings.
	 * @param language target spoken language to use
	 */
	void setLanguage(Language language);

	/**
	 * @brief Returns localized (translated) string.
	 *
	 * @param key target localized string key
	 * @param andModules also search for a string in all modules
	 */
	string_view get(string_view key, bool andModules = true) const;
	/**
	 * @brief Returns localized (translated) string.
	 * @note This function is more expensive than the UTF-8 one!
	 *
	 * @param key target localized string key
	 * @param andModules also search for a string in all modules
	 */
	string_view get(u32string_view key, bool andModules = true) const
	{
		GARDEN_ASSERT(!key.empty());
		string key32; UTF::convert(key, key32);
		return get(key32);
	}
	/**
	 * @brief Returns localized (translated) string.
	 * @note This function is more expensive than the UTF-8 one!
	 *
	 * @param key target localized string key
	 * @param[out] value localized string value
	 * @param andModules also search for a string in all modules
	 */
	void get(u32string_view key, u32string& value, bool andModules = true) const
	{
		GARDEN_ASSERT(!key.empty());
		string key8; UTF::convert(key, key8);
		auto value8 = get(key8);
		if (key8 == value8) value = key;
		else UTF::convert(value8, value);
	}

	/**
	 * @brief Returns module localized (translated) string.
	 *
	 * @param module localization module name
	 * @param key target localized string key
	 */
	string_view get(string_view module, string_view key) const
	{
		GARDEN_ASSERT(!module.empty());
		GARDEN_ASSERT(!key.empty());
		auto moduleResult = modules.find(module);
		if (moduleResult == modules.end())
			return key;
		auto stringResult = moduleResult->second.find(key);
		if (stringResult == moduleResult->second.end())
			return key;
		return stringResult->second;
	}

	/**
	 * @brief Returns true if localization module is loaded.
	 * @param module target localization module name
	 */
	bool isModuleLoaded(string_view module) const { return modules.find(module) != modules.end(); }
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
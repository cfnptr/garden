// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
 * @brief Application (game, program) information.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include <filesystem>

namespace garden
{

using namespace ecsm;
using namespace math;

/**
 * @brief Application information holder.
 */
class AppInfoSystem final : public System, public Singleton<AppInfoSystem>
{
	string name;
	string nameLowercase;
	string description;
	string creator;
	string copyright;
	Version version;

	#if GARDEN_DEBUG
	fs::path cachesPath;
	fs::path resourcesPath;
	#endif

	/*******************************************************************************************************************
	 * @brief Creates a new app info system instance.
	 * 
	 * @param[in] name application name
	 * @param[in] nameLowercase application lowercase name
	 * @param[in] description application description
	 * @param[in] creator application creator (company or author)
	 * @param[in] copyright application copyright (license)
	 * @param version application version
	 * @param setSingleton set system singleton instance
	 */
	AppInfoSystem(const string& name, const string& nameLowercase, const string& description,
		const string& creator, const string& copyright, Version version,
		#if GARDEN_DEBUG
		fs::path cachesPath, fs::path resourcesPath,
		#endif
		bool setSingleton = true);
	/**
	 * @brief Destroy app info system instance.
	 */
	~AppInfoSystem() final;
	
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Returns application name string.
	 * @details See the GARDEN_APP_NAME.
	 */
	const string& getName() const noexcept { return name; }
	/**
	 * @brief Returns application name lowercase string.
	 * @details See the GARDEN_APP_NAME_LOWERCASE.
	 */
	const string& getNameLowercase() const noexcept { return nameLowercase; }
	/**
	 * @brief Returns application description string.
	 * @details See the GARDEN_APP_DESCRIPTION.
	 */
	const string& getDescription() const noexcept { return description; }
	/**
	 * @brief Returns application creator string.
	 * @details See the GARDEN_APP_CREATOR.
	 */
	const string& getCreator() const noexcept { return creator; }
	/**
	 * @brief Returns application copyright string.
	 * @details See the GARDEN_APP_COPYRIGHT.
	 */
	const string& getCopyright() const noexcept { return copyright; }
	/**
	 * @brief Returns application version.
	 * @details See the GARDEN_APP_VERSION.
	 */
	Version getVersion() const noexcept { return version; }

	/**
	 * @brief Returns application AddData name string.
	 * @details See the GARDEN_APP_NAME, GARDEN_APP_NAME_LOWERCASE.
	 */
	const string& getAppDataName() const noexcept 
	{
		#if GARDEN_OS_LINUX
		return nameLowercase;
		#else
		return name;
		#endif
	}

	#if GARDEN_DEBUG
	/**
	 * @brief Returns application caches path. (Debug Only)
	 * @details See the GARDEN_APP_CACHES_DIR.
	 */
	const fs::path& getCachesPath() const noexcept { return cachesPath; }
	/**
	 * @brief Returns application resources path. (Debug Only)
	 * @details See the GARDEN_APP_RESOURCES_DIR.
	 */
	const fs::path& getResourcesPath() const noexcept { return resourcesPath; }
	#endif
};

} // namespace garden
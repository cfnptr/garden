// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
 * @brief File changes watching functions.
 */

#pragma once
#include "garden/defines.hpp"

#if GARDEN_DEBUG || GARDEN_EDITOR
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief System watching for a file changes.
 */
class FileWatcherSystem final : public System, public Singleton<FileWatcherSystem>
{
	void* instance = nullptr;
	vector<fs::path> changedFiles;
	vector<fs::path> createdFiles;
	fs::path currentFilePath = {};
	#if GARDEN_OS_LINUX
	tsl::robin_map<int, fs::path> watchers;
	#endif
	#if GARDEN_OS_MACOS
	mutex locker = {};
	#endif

	/**
	 * @brief Creates a new file watcher system instance.
	 * @param setSingleton set system singleton instance
	 */
	FileWatcherSystem(bool setSingleton = true);
	/**
	 * @brief Destroys file watcher system instance.
	 */
	~FileWatcherSystem() final;

	void preInit();
	void postDeinit();
	void update();

	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns current changed/created file path.
	 * @details Expected to use this inside the "FileChange", "FileCreate" events.
	 */
	const fs::path& getFilePath() const noexcept { return currentFilePath; }

	/**
	 * @brief Returns current changed file paths.
	 */
	vector<fs::path>& getChangedFiles() noexcept { return changedFiles; }
	/**
	 * @brief Returns current changed file paths.
	 */
	vector<fs::path>& getCreatedFiles() noexcept { return createdFiles; }
};

} // namespace garden
#endif
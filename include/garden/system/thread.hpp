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
 * @brief Common multithreading functions.
 */

#pragma once
#include "garden/thread-pool.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief Thread pool holder. (Asynchronous task executor)
 * 
 * @details
 * A thread system is used to manage and reuse a pool of worker threads for executing tasks asynchronously.
 * Threads allow for concurrent execution of code within a single process, enabling multitasking and parallelism.
 */
class ThreadSystem final : public System, public Singleton<ThreadSystem>
{
	ThreadPool backgroundPool;
	ThreadPool foregroundPool;

	/**
	 * @brief Creates a new thread system instance.
	 * @param setSingleton set system singleton instance
	 */
	ThreadSystem(bool setSingleton = true);
	/**
	 * @brief Destroys thread system instance.
	 */
	~ThreadSystem() final;

	void preInit();
	void preDeinit();

	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns background thread pool instance.
	 * @details Use it to add async background tasks, which can take several frames.
	 */
	ThreadPool& getBackgroundPool() noexcept { return backgroundPool; }
	/**
	 * @brief Returns foreground thread pool instance.
	 * @details Use it to parallel some jobs during current frame.
	 */
	ThreadPool& getForegroundPool() noexcept { return foregroundPool; }
};

} // namespace garden
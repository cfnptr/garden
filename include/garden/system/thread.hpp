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
 * @brief Common multithreading functions.
 */

#pragma once
#include "ecsm.hpp"
#include "garden/thread-pool.hpp"
#include "mpmt/thread.hpp"

namespace garden
{

using namespace std;
using namespace ecsm;

/***********************************************************************************************************************
 * @brief Thread pool holder.
 * 
 * @details
 * A thread system is used to manage and reuse a pool of worker threads for executing tasks asynchronously.
 * Threads allow for concurrent execution of code within a single process, enabling multitasking and parallelism.
 */
class ThreadSystem final : public System
{
	ThreadPool backgroundPool;
	ThreadPool foregroundPool;

	/**
	 * @brief Creates a new thread system instance.
	 */
	ThreadSystem() : backgroundPool(true, "BG"), foregroundPool(false, "FG")
	{
		mpmt::Thread::setForegroundPriority();
		SUBSCRIBE_TO_EVENT("PreDeinit", ThreadSystem::preDeinit);
	}
	/**
	 * @brief Destroys thread system instance.
	 */
	~ThreadSystem() final
	{
		if (Manager::get()->isRunning())
			UNSUBSCRIBE_FROM_EVENT("PreDeinit", ThreadSystem::preDeinit);
	}
		
	void preDeinit()
	{
		backgroundPool.removeAll();
		backgroundPool.wait();
	}

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
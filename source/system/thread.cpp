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

#include "garden/system/thread.hpp"
#include "garden/system/log.hpp"
#include "garden/os.hpp"

using namespace garden;

//**********************************************************************************************************************
ThreadSystem::ThreadSystem(bool setSingleton) : Singleton(setSingleton),
	backgroundPool(true, "BG", mpio::OS::getPhysicalCpuCount()),
	foregroundPool(false, "FG", getBestForegroundThreadCount())
{
	mpmt::Thread::setMain();

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", ThreadSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", ThreadSystem::preDeinit);
}
ThreadSystem::~ThreadSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", ThreadSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", ThreadSystem::preDeinit);
	}
	unsetSingleton();
}

void ThreadSystem::preInit()
{
	GARDEN_LOG_INFO("Background thread pool size: " + to_string(backgroundPool.getThreadCount()));
	GARDEN_LOG_INFO("Foreground thread pool size: " + to_string(foregroundPool.getThreadCount()));
}
void ThreadSystem::preDeinit()
{
	backgroundPool.wait();
}
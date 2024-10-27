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
#include "mpio/os.hpp"

using namespace mpio;
using namespace garden;

static int getBestForegroundThreadCount()
{
	auto cpuName = OS::getCpuName();
	if (cpuName.find("AMD") != string::npos)
	{
		// Only one of the two CCXes has additional 3D V-Cache.
		if (cpuName.find("9 7950X3D") != string::npos || cpuName.find("9 7900X3D") != string::npos ||
			cpuName.find("9 7945HX3D") != string::npos) 
		{
			auto cpuCount = OS::getPhysicalCpuCount();
			return cpuCount > 1 ? cpuCount / 2 : cpuCount;
		}
	}
	return OS::getPerformanceCpuCount();
}

//**********************************************************************************************************************
ThreadSystem::ThreadSystem(bool setSingleton) : Singleton(setSingleton),
	backgroundPool(true, "BG", OS::getLogicalCpuCount()),
	foregroundPool(false, "FG", getBestForegroundThreadCount())
{
	mpmt::Thread::setForegroundPriority();
	ECSM_SUBSCRIBE_TO_EVENT("PreDeinit", ThreadSystem::preDeinit);
}
ThreadSystem::~ThreadSystem()
{
	if (Manager::Instance::get()->isRunning())
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDeinit", ThreadSystem::preDeinit);
	unsetSingleton();
}

void ThreadSystem::preDeinit()
{
	backgroundPool.removeAll();
	backgroundPool.wait();
}
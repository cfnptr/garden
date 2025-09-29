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

#include "garden/system/loop.hpp"
#include "garden/profiler.hpp"
#include "mpio/os.hpp"
#include "mpmt/thread.hpp"

#include <thread>

using namespace garden;

#if GARDEN_OS_WINDOWS
#include <windows.h>

static BOOL WINAPI consoleHandler(DWORD ctrlType)
{
	switch (ctrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		Manager::Instance::get()->isRunning = false;
		return TRUE;
	default: return FALSE;
	}
}
#elif GARDEN_OS_LINUX || GARDEN_OS_MACOS
#include <csignal>

static void signalHandler(int signum)
{
	Manager::Instance::get()->isRunning = false;
}
#else
#error Unknown operating system
#endif

LoopSystem::LoopSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get(); 
	manager->registerEventBefore("Input", "Update");
	manager->registerEventAfter("Output", "Update");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", LoopSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", LoopSystem::deinit);

	#if GARDEN_OS_LINUX || GARDEN_OS_MACOS
	signal(SIGINT, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGTERM, signalHandler);
	#else
	SetConsoleCtrlHandler(consoleHandler, TRUE);
	#endif
}
LoopSystem::~LoopSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", LoopSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", LoopSystem::deinit);

		auto manager = Manager::Instance::get(); 
		manager->unregisterEvent("Input");
		manager->unregisterEvent("Output");
	}
	unsetSingleton();
}

void LoopSystem::preInit()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", LoopSystem::input);
	ECSM_SUBSCRIBE_TO_EVENT("Output", LoopSystem::output);

	systemTime = mpio::OS::getCurrentClock();
}
void LoopSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", LoopSystem::input);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Output", LoopSystem::output);
	}
}

void LoopSystem::input()
{
	SET_CPU_ZONE_SCOPED("Loop Input");

	auto time = mpio::OS::getCurrentClock();
	deltaTime = (time - systemTime) * timeMultiplier;
	currentTime += deltaTime;
	systemTime = time;
}
void LoopSystem::output()
{
	SET_CPU_ZONE_SCOPED("Loop Sleep");

	auto deltaClock = mpio::OS::getCurrentClock() - systemTime;
	auto delayTime = (1.0 / maxTickRate) - deltaClock - 0.001;
	if (delayTime > 0.0)
		mpmt::Thread::sleep(delayTime);
}
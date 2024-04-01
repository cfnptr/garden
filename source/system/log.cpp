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

#include "garden/system/log.hpp"
#include "garden/system/app-info.hpp"
#include "garden/file.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/log.hpp"
#endif

#include "mpmt/thread.hpp"
#include "mpio/os.hpp"
#include <thread>

using namespace mpio;
using namespace garden;

//**********************************************************************************************************************
LogSystem* LogSystem::instance = nullptr;

LogSystem::LogSystem(Manager* manager, LogLevel level, double rotationTime) : System(manager)
{
	#if GARDEN_EDITOR
	SUBSCRIBE_TO_EVENT("PreInit", LogSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostDeinit", LogSystem::postDeinit);
	#endif

	auto appInfoSystem = manager->get<AppInfoSystem>();

	this->logger = logy::Logger(appInfoSystem->getAppDataName(),
		level, GARDEN_DEBUG ? true : false, rotationTime);

	mpmt::Thread::setName("MAIN");
	info("Started logging system. (UTC+0)");
	info(appInfoSystem->getName() + " [v" + appInfoSystem->getVersion().toString3() + "]");
	info(GARDEN_NAME_STRING " Engine [v" GARDEN_VERSION_STRING "]");
	info("OS: " GARDEN_OS_NAME " (" GARDEN_ARCH ")");
	info("SIMDs: " + string(GARDEN_SIMD_STRING));
	info("CPU: " + string(OS::getCpuName()));
	info("Thread count: " + to_string(thread::hardware_concurrency()));
	info("Total RAM size: " + toBinarySizeString(OS::getTotalRamSize()));
	info("Free RAM size: " + toBinarySizeString(OS::getFreeRamSize()));

	instance = this;
}
LogSystem::~LogSystem()
{
	#if GARDEN_EDITOR
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", LogSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", LogSystem::postDeinit);
	}
	#endif

	logger.log(INFO_LOG_LEVEL, "Stopped logging system.");
}

#if GARDEN_EDITOR
//**********************************************************************************************************************
void LogSystem::preInit()
{
	auto manager = getManager();
	if (manager->has<EditorRenderSystem>())
		manager->createSystem<LogEditorSystem>(this);
}
void LogSystem::postDeinit()
{
	getManager()->tryDestroySystem<LogEditorSystem>();
}
#endif

void LogSystem::log(LogLevel level, const string& message) noexcept
{
	logger.log(level, "%.*s", message.length(), message.c_str());

	#if GARDEN_EDITOR
	auto logEditorSystem = getManager()->tryGet<LogEditorSystem>();
	if (logEditorSystem)
		logEditorSystem->log(level, message);
	#endif
}
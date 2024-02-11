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
#include "garden/file.hpp"
#include "garden/simd.hpp"
#include "garden/os.hpp"

#include "mpmt/thread.hpp"
#include "mpio/os.hpp"
#include <thread>

using namespace mpio;
using namespace garden;

#if GARDEN_DEBUG
LogSystem* LogSystem::instance;
#endif

//**********************************************************************************************************************
static void logSystemInfo(LogSystem* logSystem)
{
	logSystem->info("Started logging system. (UTC+0)");
	logSystem->info(GARDEN_APP_NAME_STRING " version: " GARDEN_VERSION_STRING);
	logSystem->info("OS: " GARDEN_OS_NAME);
	logSystem->info("SIMDs: " + string(GARDEN_SIMD_STRING));
	logSystem->info("CPU: " + string(OS::getCpuName()));
	logSystem->info("Thread count: " + to_string(thread::hardware_concurrency()));
	logSystem->info("Total RAM size: " + toBinarySizeString(OS::getTotalRamSize()));
	logSystem->info("Free RAM size: " + toBinarySizeString(OS::getFreeRamSize()));
}

LogSystem::LogSystem(Manager* manager, LogLevel level) : System(manager)
{
	#if GARDEN_OS_LINUX
	auto appName = GARDEN_APP_NAME_LOWERCASE_STRING;
	#else
	auto appName = GARDEN_APP_NAME_STRING;
	#endif

	this->logger = logy::Logger(appName, level, GARDEN_DEBUG ? true : false);

	mpmt::Thread::setName("MAIN");
	logSystemInfo(this);

	#if GARDEN_DEBUG
	instance = this;
	#endif
}
LogSystem::~LogSystem()
{
	info("Stopped logging system.");
}

void LogSystem::log(LogLevel level, const string& message) noexcept
{
	logger.log(level, "%.*s", message.length(), message.c_str());
}
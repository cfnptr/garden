//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/log.hpp"
#include "garden/file.hpp"
#include "mpio/os.hpp"
#include <thread>

extern "C"
{
#include "mpmt/thread.h"
};

using namespace mpio;
using namespace garden;

//--------------------------------------------------------------------------------------------------
LogSystem::LogSystem(LogLevel level)
{
	#if __linux__
	auto appName = GARDEN_APP_NAME_LOWERCASE_STRING;
	#else
	auto appName = GARDEN_APP_NAME_STRING;
	#endif

	this->logger = logy::Logger(appName, level,
		GARDEN_DEBUG ? true : false);

#if __linux__
	auto osName = "Linux";
#elif __APPLE__
	auto osName = "macOS";
#else
	auto osName = "Windows";
#endif

	setThreadName("MAIN");
	info("Started logging system. (UTC+0)");
	info(GARDEN_APP_NAME_STRING " version: " GARDEN_VERSION_STRING);
	info("OS: " + string(osName));
	info("CPU: " + string(OS::getCpuName()));
	info("Thread count: " + to_string(thread::hardware_concurrency()));
	info("RAM size: " + toBinarySizeString(OS::getRamSize()));
}
LogSystem::~LogSystem()
{
	info("Stopped logging system.");
}
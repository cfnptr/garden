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
#include "mpio/directory.hpp"
#include <thread>

extern "C"
{
#include "mpio/os.h"
}

using namespace mpio;
using namespace garden;

//--------------------------------------------------------------------------------------------------
LogSystem::LogSystem(Severity severity)
{
	this->severity = severity;

	auto appDataPath = Directory::getAppDataPath(GARDEN_APP_NAME_LOWERCASE_STRING);
	fs::create_directory(appDataPath);
	fileStream.open(appDataPath / "log.txt");

	if (!fileStream.is_open())
		throw runtime_error("Failed to open log file stream.");

	Thread::setName("MAIN");
	auto concurrentThreadCount = thread::hardware_concurrency();
	info("Started logging system. (UTC+0)");
	info(GARDEN_APP_NAME_STRING " version: " GARDEN_VERSION_STRING);
	info("Concurrent thread count: " + to_string(concurrentThreadCount));
	info("RAM size: " + to_string(getRamSize()));
	info("CPU: " + string(getCpuName()));
}
LogSystem::~LogSystem()
{
	info("Stopped logging system.");
}
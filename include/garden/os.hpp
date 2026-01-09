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
 * @brief Target operating system defines.
 */

#pragma once
#include "mpio/os.hpp"

#if _WIN32
#define GARDEN_OS_WINDOWS 1
#define GARDEN_OS_NAME "Windows"
#else
#define GARDEN_OS_WINDOWS 0
#endif

#if __APPLE__
#define GARDEN_OS_MACOS 1
#define GARDEN_OS_NAME "macOS"
#else
#define GARDEN_OS_MACOS 0
#endif

#if __linux__
#define GARDEN_OS_LINUX 1
#define GARDEN_OS_NAME "Linux"
#else
#define GARDEN_OS_LINUX 0
#endif

#if !GARDEN_OS_WINDOWS && !GARDEN_OS_MACOS && !GARDEN_OS_LINUX
#error Unknown operating system
#endif

namespace garden
{

/**
 * @brief Returns best foreground thread count for a system CPU.
 */
static int getBestForegroundThreadCount()
{
	auto cpuName = mpio::OS::getCpuName();
	if (cpuName.find("AMD") != std::string::npos)
	{
		// Only one of the two CCXes has additional 3D V-Cache.
		if (cpuName.find("9950X3D") != std::string::npos ||
			cpuName.find("9900X3D") != std::string::npos ||
			cpuName.find("7950X3D") != std::string::npos ||
			cpuName.find("7900X3D") != std::string::npos ||
			cpuName.find("7945HX3D") != std::string::npos)
		{
			auto cpuCount = mpio::OS::getPhysicalCpuCount();
			return cpuCount > 1 ? cpuCount / 2 : cpuCount;
		}
		// TODO: Maybe we can detect CCX count? Also support server AMD CPUs.
	}
	return mpio::OS::getPerformanceCpuCount();
}

} // namespace garden
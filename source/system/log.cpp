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

#include "garden/system/log.hpp"
#include "garden/system/app-info.hpp"
#include "garden/file.hpp"
#include "garden/os.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/log.hpp"
#endif

#include "mpmt/thread.hpp"
#include <thread>
#include <chrono>

#if GARDEN_OS_LINUX || GARDEN_OS_MACOS
#include <sys/utsname.h>
#elif GARDEN_OS_WINDOWS
#include <windows.h>
#pragma comment(lib, "Version.lib")
#endif

using namespace garden;

//**********************************************************************************************************************
static void logKernelInfo(LogSystem* logSystem)
{
	#if GARDEN_OS_LINUX || GARDEN_OS_MACOS
	struct utsname info;
	memset(&info, 0, sizeof(struct utsname));
	auto result = uname(&info);
	if (result != 0)
		throw GardenError("Failed to get kernel information.");
	logSystem->info("OS name: " + string(info.sysname));
	logSystem->info("OS release: " + string(info.release));
	logSystem->info("OS version: " + string(info.version));
	logSystem->info("OS machine: " + string(info.machine));
	#elif GARDEN_OS_WINDOWS
	auto kernelDLL = L"kernel32.dll"; DWORD dummy = 0;
	auto infoSize = GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, kernelDLL, &dummy);
	vector<char> buffer(infoSize);
	auto result = GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, kernelDLL, dummy, buffer.size(), buffer.data());
	if (!result)
		throw GardenError("Failed to get kernel file version information.");
	void* info = nullptr; UINT size = 0;
	result = VerQueryValueW(buffer.data(), L"\\", &info, &size);
	if (!result)
		throw GardenError("Failed to get kernel version.");
	auto fileInfo = (const VS_FIXEDFILEINFO*)info;
	auto osName = HIWORD(fileInfo->dwFileVersionMS) == 10 && HIWORD(fileInfo->dwFileVersionLS) >= 22000 ?
		string("Windows 11") : "Windows " + to_string(HIWORD(fileInfo->dwFileVersionMS));
	logSystem->info("OS name: " + osName);
	logSystem->info("OS version: " + 
		to_string(HIWORD(fileInfo->dwFileVersionMS)) + "." + to_string(LOWORD(fileInfo->dwFileVersionMS)) + "." + 
		to_string(HIWORD(fileInfo->dwFileVersionLS)) + "." + to_string(LOWORD(fileInfo->dwFileVersionLS)));
	#endif
}

static string getCurrentDate()
{
	auto currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
	auto timeString = string(std::ctime(&currentTime));
	if (timeString.size() > 0)
		timeString.resize(timeString.size() - 1);
	return timeString;
}

//**********************************************************************************************************************
LogSystem::LogSystem(LogLevel level, double rotationTime, bool setSingleton) : Singleton(setSingleton)
{
	mpmt::Thread::setName("MAIN");
	auto appInfoSystem = AppInfoSystem::Instance::get();

	try
	{
		this->logger = logy::Logger(appInfoSystem->getAppDataName(),
			level, GARDEN_DEBUG ? true : false, rotationTime);
	}
	catch (exception& e)
	{
		auto tmpPath = fs::path(std::tmpnam(nullptr));
		this->logger = logy::Logger(appInfoSystem->getAppDataName() / tmpPath.filename(),
			level, GARDEN_DEBUG ? true : false, rotationTime);
	}

	info("Started logging system. (UTC+0)");
	info("Date: " + getCurrentDate());
	info(appInfoSystem->getName() + " [v" + appInfoSystem->getVersion().toString3() + "]");
	info(GARDEN_NAME_STRING " Engine [v" GARDEN_VERSION_STRING "]");
	info("Target OS: " GARDEN_OS_NAME " (" GARDEN_CPU_ARCH ")");
	logKernelInfo(this);
	info("SIMDs: " + string(GARDEN_SIMD_STRING));
	info("CPU: " + string(mpio::OS::getCpuName()));
	info("Logical core count: " + to_string(mpio::OS::getLogicalCpuCount()));
	info("Physical core count: " + to_string(mpio::OS::getPhysicalCpuCount()));
	info("Performance core count: " + to_string(getBestForegroundThreadCount()));
	info("Total RAM size: " + toBinarySizeString(mpio::OS::getTotalRamSize()));
	info("Free RAM size: " + toBinarySizeString(mpio::OS::getFreeRamSize()));
}
LogSystem::~LogSystem()
{
	info("Stopped logging system.");
	unsetSingleton();
}

void LogSystem::log(LogLevel level, const string& message) noexcept
{
	logger.log(level, "%.*s", message.length(), message.c_str());

	#if GARDEN_EDITOR
	auto logEditorSystem = LogEditorSystem::Instance::tryGet();
	if (logEditorSystem)
		logEditorSystem->log(level, message);
	#endif
}
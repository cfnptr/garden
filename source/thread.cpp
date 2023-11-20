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

#include "garden/thread.hpp"
#include "math/types.hpp"
#include <stdexcept>

#if __linux__ || __APPLE__
#include <pthread.h>
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#else
#error Unsupported operating system
#endif

using namespace garden;

//--------------------------------------------------------------------------------------------------
string Thread::getName()
{
	char name[17];
	#if __linux__ || __APPLE__
	auto result = pthread_getname_np(pthread_self(), name, 16);
	if (result != 0)
		throw runtime_error("Failed to get thread name. (" + to_string(result) + ")");
	#elif _WIN32
	PWSTR wideName = nullptr;
	auto result = GetThreadDescription(GetCurrentThread(), &wideName);

	if (FAILED(result))
		throw runtime_error("Failed to get thread name. (" + to_string(result) + ")");
	
	for (uint8 i = 0; i < 16; i++)
	{
		name[i] = (char)wideName[i];
		if (name[i] == '\0') break;
	}
	#endif
	return string(name);
}
void Thread::setName(const string& name)
{
	if (name.length() > 15) throw runtime_error("Thread name is too long.");
	#if __linux__
	auto result = pthread_setname_np(pthread_self(), name.c_str());
	if (result != 0)
		throw runtime_error("Failed to set thread name. (" + to_string(result) + ")");
	#elif __APPLE__
	auto result = pthread_setname_np(name.c_str());
	if (result != 0)
		throw runtime_error("Failed to set thread name. (" + to_string(result) + ")");
	#elif _WIN32
	wstring wideName(name.begin(), name.end());
	auto result = SetThreadDescription(GetCurrentThread(), wideName.c_str());
	if (FAILED(result))
		throw runtime_error("Failed to set thread name. (" + to_string(result) + ")");
	#endif
}

//--------------------------------------------------------------------------------------------------
void Thread::setForegroundPriority()
{
	#if __linux__ || __APPLE__
	struct sched_param param; int policy;
	auto result = pthread_getschedparam(pthread_self(), &policy, &param);
	if (result != 0) throw runtime_error("Failed to set max thread priority.");
	if (param.sched_priority + 1 < sched_get_priority_max(policy))
		param.sched_priority++;
	result = pthread_setschedparam(pthread_self(), policy, &param);
	if (result != 0) throw runtime_error("Failed to set max thread priority.");
	#elif _WIN32
	auto result = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	if (!result) throw runtime_error("Failed to set hight thread priority.");
	#endif
}
void Thread::setBackgroundPriority()
{
	#if __linux__ || __APPLE__
	struct sched_param param; int policy;
	auto result = pthread_getschedparam(pthread_self(), &policy, &param);
	if (result != 0) throw runtime_error("Failed to set min thread priority.");
	if (param.sched_priority - 1 > sched_get_priority_min(policy))
		param.sched_priority--;
	result = pthread_setschedparam(pthread_self(), policy, &param);
	if (result != 0) throw runtime_error("Failed to set min thread priority.");
	#elif _WIN32
	auto result = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	if (!result) throw runtime_error("Failed to set low thread priority.");
	#endif
}
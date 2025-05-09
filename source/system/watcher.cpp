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

// TODO: refactor this

// TODO: if file is .gsl shader header then iterate over all shader files
// by loading them into memory as strings and search for specific shader header.
// then add to reload list. Also constantly check if there is no new requests for file refresh
// when background thread processes it.

/*
#include "garden/system/watcher.hpp"

#if GARDEN_OS_LINUX
//#error TODO: implement
#elif GARDEN_OS_MACOS
#include <CoreServices/CoreServices.h>
#elif GARDEN_OS_WINDOWS
//#error TODO: implement
#else
#error Unknown operating system
#endif

using namespace math;
using namespace garden;

//--------------------------------------------------------------------------------------------------
#if GARDEN_OS_MACOS
static void onChange(ConstFSEventStreamRef streamRef,
	void* clientCallBackInfo, psize numEvents, void* eventPaths,
	const FSEventStreamEventFlags eventFlags[], const FSEventStreamEventId eventIds[])
{
	auto watcherSystem = (WatcherSystem*)clientCallBackInfo;
	const auto& changedFiles = watcherSystem->getChangedFiles();
	auto& locker = watcherSystem->getLocker();
	auto paths = (const char**)eventPaths;

	locker.lock();
	for (psize i = 0; i < numEvents; i++)
	{
		if (eventFlags[i] & (kFSEventStreamEventFlagItemModified |
			kFSEventStreamEventFlagItemIsFile))
		{
			changedFiles.push_back(string(paths[i]));
		}
	}
	locker.unlock();
}
#endif

//--------------------------------------------------------------------------------------------------
void WatcherSystem::initialize()
{
	#if GARDEN_OS_MACOS
	auto path = "TODO"; // GARDEN_PROJECT_PATH / "resources";
	auto pathToWatch = CFStringCreateWithCString(
		kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
	auto pathsToWatch = CFArrayCreate(nullptr, (const void**)&pathToWatch, 1, nullptr);

	FSEventStreamContext context;
	context.version = 0;
	context.info = this;
	context.retain = nullptr;
	context.release = nullptr;
	context.copyDescription = nullptr;

	auto stream = FSEventStreamCreate(nullptr, &onChange, &context, pathsToWatch,
		kFSEventStreamEventIdSinceNow, 1.0, kFSEventStreamCreateFlagFileEvents);
	this->instance = stream;

	dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	FSEventStreamSetDispatchQueue(stream, queue);
	FSEventStreamStart(stream);
	CFRelease(pathsToWatch);
	CFRelease(pathToWatch);
	#endif

	GARDEN_LOG_INFO("Started watcher system.");
}
void WatcherSystem::terminate()
{
	#if GARDEN_OS_MACOS
	auto stream = (FSEventStreamRef)instance;
	FSEventStreamStop(stream);
	FSEventStreamInvalidate(stream);
	FSEventStreamRelease(stream);
	#endif

	GARDEN_LOG_INFO("Stopped watcher system.");
}

//--------------------------------------------------------------------------------------------------
void WatcherSystem::update()
{
	locker.lock();
	if (!changedFiles.empty())
	{
		for (const auto& changedFile : changedFiles)
		{
			GARDEN_LOG_TRACE("Changed file. (" + changedFile + ")");
			
			auto extensionIndex = changedFile.find_last_of('.');
			auto extension = extensionIndex == string::npos ? "" : string(changedFile,
				extensionIndex + 1, changedFile.length() - (extensionIndex + 1));
			auto searchResult = listeners.find(extension);

			if (searchResult != listeners.end())
			{
				for (const auto& listener : searchResult->second)
					listener(changedFile);
			}
		}
		changedFiles.clear();
	}
	locker.unlock();
}
*/
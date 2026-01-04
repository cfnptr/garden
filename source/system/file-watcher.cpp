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

#include "garden/system/file-watcher.hpp"

#if GARDEN_DEBUG || GARDEN_EDITOR
#include "garden/system/app-info.hpp"
#include "garden/system/log.hpp"

#if GARDEN_OS_LINUX
#include <unistd.h>
#include <sys/inotify.h>
#elif GARDEN_OS_MACOS
#include <CoreServices/CoreServices.h>
#elif GARDEN_OS_WINDOWS
//#error TODO: implement
#else
#error Unknown operating system
#endif

using namespace math;
using namespace garden;

#if GARDEN_OS_LINUX
static void flushChanges(void* instance, tsl::robin_map<int, fs::path>& watchers,
	vector<fs::path>& changedFiles, vector<fs::path>& createdFiles)
{
	if (!instance)
		return;

	auto fd = (int)(size_t)instance;
	static constexpr auto bufferSize = sizeof(struct inotify_event) + NAME_MAX + 1;
	char buffer[bufferSize];

	while (true)
	{
		auto length = read(fd, buffer, bufferSize);
		if (length == 0 || length == -1)
		{
			if (length == -1 && errno != EAGAIN)
				GARDEN_LOG_ERROR("Failed to read inotify. (error: " + to_string(errno) + ")");
			break;
		}

		uint32 offset = 0;
		while (offset < length)
		{
			auto event = (const struct inotify_event*)(buffer + offset);
			if (event->mask & IN_ISDIR) 
			{
				if (event->mask & IN_DELETE_SELF)
				{
					auto searchResult = watchers.find(event->wd);
					if (searchResult != watchers.end())
						watchers.erase(searchResult);
				}
				// TODO: add new created and moved dirs to the watcher.
			}
			else if (event->mask & (IN_CREATE | IN_MODIFY) && event->len > 0)
			{
				auto searchResult = watchers.find(event->wd);
				if (searchResult != watchers.end())
				{
					if (event->mask & IN_CREATE)
						createdFiles.push_back(searchResult->second / event->name);
					else if (event->mask & IN_MODIFY)
						changedFiles.push_back(searchResult->second /event->name);
					// TODO: moved files
				}
			}
			offset += sizeof(struct inotify_event) + event->len;
		}
	}
}
#elif GARDEN_OS_MACOS
//**********************************************************************************************************************
static void onChange(ConstFSEventStreamRef streamRef,
	void* clientCallBackInfo, psize numEvents, void* eventPaths,
	const FSEventStreamEventFlags eventFlags[], const FSEventStreamEventId eventIds[])
{
	auto fileWatcherSystem = (FileWatcherSystem*)clientCallBackInfo;
	auto& changedFiles = fileWatcherSystem->getChangedFiles();
	auto& createdFiles = fileWatcherSystem->getCreatedFiles();
	auto& locker = fileWatcherSystem->getLocker();
	auto paths = (const char**)eventPaths;

	locker.lock();
	for (psize i = 0; i < numEvents; i++)
	{
		auto flags = eventFlags[i];
		if (!(flags & kFSEventStreamEventFlagItemIsFile))
			continue;

		if (flags & kFSEventStreamEventFlagItemCreated)
			createdFiles.push_back(string(paths[i]));
		else if (flags & kFSEventStreamEventFlagItemModified)
			changedFiles.push_back(string(paths[i]));
		// TODO: moved files
	}
	locker.unlock();
}
#elif GARDEN_OS_WINDOWS
// TODO:
#endif

//**********************************************************************************************************************
FileWatcherSystem::FileWatcherSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("FileChange");
	manager->registerEvent("FileCreate");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", FileWatcherSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostDeinit", FileWatcherSystem::postDeinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", FileWatcherSystem::update);
}
FileWatcherSystem::~FileWatcherSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->unregisterEvent("FileChange");
		manager->unregisterEvent("FileCreate");

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", FileWatcherSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostDeinit", FileWatcherSystem::postDeinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", FileWatcherSystem::update);
	}

	unsetSingleton();
}

#if GARDEN_OS_LINUX
static bool addDirWatchers(int fd, const fs::path& resourcesPath, tsl::robin_map<int, fs::path>& watchers)
{
	constexpr auto watchMask = IN_CREATE | IN_DELETE_SELF | IN_MODIFY | IN_MOVED_TO;
	auto wd = inotify_add_watch(fd, GARDEN_RESOURCES_PATH.c_str(), watchMask);
	if (wd == -1)
	{
		GARDEN_LOG_ERROR("Failed to add inotify watch. (error: " + to_string(errno) + ")");
		return false;
	}
	watchers.emplace(wd, resourcesPath);

	try
	{
		for (const auto& entry : fs::recursive_directory_iterator(resourcesPath))
		{
			if (!entry.is_directory())
				continue;

			wd = inotify_add_watch(fd, entry.path().c_str(), watchMask);
			if (wd == -1)
			{
				GARDEN_LOG_ERROR("Failed to add inotify watch. (error: " + to_string(errno) + ")");
				continue;
			}
			watchers.emplace(wd, entry.path());
		}
	}
	catch (exception& e)
	{
		GARDEN_LOG_ERROR("Failed to add inotify watches. (error: " + string(e.what()) + ")");
		return false;
	}
	return true;
}
#endif

void FileWatcherSystem::preInit()
{
	auto appInfoSystem = AppInfoSystem::Instance::get();
	auto appResourcesPath = appInfoSystem->getResourcesPath();

	#if GARDEN_OS_LINUX
	auto fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC); // TODO: we can use separate thread instead?
	if (fd == -1)
	{
		GARDEN_LOG_ERROR("Failed to initilize inotify. (error: " + to_string(errno) + ")");
		return;
	}
	instance = (void*)(size_t)fd;

	addDirWatchers(fd, GARDEN_RESOURCES_PATH, watchers);
	addDirWatchers(fd, appResourcesPath, watchers);
	#elif GARDEN_OS_MACOS
	auto pathToWatch = CFStringCreateWithCString(kCFAllocatorDefault, 
		appResourcesPath.c_str(), kCFStringEncodingUTF8);
	// TODO: also watch for the GARDEN_RESOURCES_PATH!
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
	#elif GARDEN_OS_WINDOWS
	// TODO:
	#endif
}
void FileWatcherSystem::postDeinit()
{
	if (instance)
	{
		#if GARDEN_OS_LINUX
		close((int)(size_t)instance);
		#elif GARDEN_OS_MACOS
		auto stream = (FSEventStreamRef)instance;
		FSEventStreamStop(stream);
		FSEventStreamInvalidate(stream);
		FSEventStreamRelease(stream);
		#elif GARDEN_OS_WINDOWS
		// TODO:
		#endif
	}
}

//**********************************************************************************************************************
void FileWatcherSystem::update()
{
	#if GARDEN_OS_MACOS
	locker.lock();
	#endif

	#if GARDEN_OS_LINUX
	flushChanges(instance, watchers, changedFiles, createdFiles);
	#endif

	auto manager = Manager::Instance::get();
	if (!changedFiles.empty())
	{
		auto event = manager->getEvent("FileChange");
		if (event.hasSubscribers())
		{
			for (const auto& filePath : changedFiles)
			{
				GARDEN_LOG_TRACE("Detected file changes: " + filePath.generic_string());
				currentFilePath = filePath;
				event.run();
			}
		}
		changedFiles.clear();
	}
	if (!createdFiles.empty())
	{
		auto event = manager->getEvent("FileCreate");
		if (event.hasSubscribers())
		{
			for (const auto& filePath : createdFiles)
			{
				GARDEN_LOG_TRACE("Detected a new file: " + filePath.generic_string());
				currentFilePath = filePath;
				event.run();
			}
		}
		createdFiles.clear();
	}

	#if GARDEN_OS_MACOS
	locker.unlock();
	#endif
}
#endif
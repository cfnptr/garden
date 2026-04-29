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
#elif GARDEN_OS_APPLE
#include <CoreServices/CoreServices.h>
#elif GARDEN_OS_WINDOWS
#include <windows.h>
#endif

using namespace math;
using namespace garden;

#if GARDEN_OS_LINUX
//**********************************************************************************************************************
static void flushChanges(void* instance, tsl::robin_map<int, fs::path>& watchers,
	set<fs::path>& changedFiles, set<fs::path>& createdFiles)
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
						createdFiles.emplace(searchResult->second / event->name);
					else if (event->mask & IN_MODIFY)
						changedFiles.emplace(searchResult->second /event->name);
					// TODO: moved files
				}
			}
			offset += sizeof(struct inotify_event) + event->len;
		}
	}
}
#elif GARDEN_OS_APPLE
//**********************************************************************************************************************
static void onChange(ConstFSEventStreamRef streamRef, void* clientCallBackInfo, psize numEvents, void* eventPaths,
	const FSEventStreamEventFlags eventFlags[], const FSEventStreamEventId eventIds[])
{
	auto fileWatcherSystem = (FileWatcherSystem*)clientCallBackInfo;
	auto& changedFiles = fileWatcherSystem->getChangedFiles();
	auto& createdFiles = fileWatcherSystem->getCreatedFiles();
	auto& locker = fileWatcherSystem->getLocker_();
	auto paths = (const char**)eventPaths;

	locker.lock();
	for (psize i = 0; i < numEvents; i++)
	{
		auto flags = eventFlags[i];
		if (!(flags & kFSEventStreamEventFlagItemIsFile))
			continue;

		if (flags & kFSEventStreamEventFlagItemCreated)
			createdFiles.emplace(string(paths[i]));
		else if (flags & kFSEventStreamEventFlagItemModified)
			changedFiles.emplace(string(paths[i]));
		// TODO: moved files
	}
	locker.unlock();
}
#elif GARDEN_OS_WINDOWS
namespace
{
	struct WatcherDir final
	{
		OVERLAPPED overlapped;
		HANDLE dirHandle;
		vector<char> buffer;
		fs::path path;
	};
	struct WatcherData final
	{
		vector<WatcherDir> watchers;
		thread thread;
		FileWatcherSystem* system;
	};
}

static bool enqueueDirWatcher(WatcherDir& watcher)
{
	auto result = ReadDirectoryChangesW(watcher.dirHandle, watcher.buffer.data(),
		(DWORD)watcher.buffer.size(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | 
		FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &watcher.overlapped, NULL);
	if (!result)
	{
		GARDEN_LOG_ERROR("Failed to enqueue directory watcher. (error: " + to_string(GetLastError()) + ")");
		return false;
	}
	return true;
}
static void fileWatcherThread(WatcherData* data)
{
	auto& watchers = data->watchers;
	vector<HANDLE> events(watchers.size());

	for (psize i = 0; i < watchers.size(); i++)
	{
		auto& watcher = watchers[i];
		if (!enqueueDirWatcher(watcher))
			return;
		events[i] = watcher.overlapped.hEvent;
	}

	auto fileWatcherSystem = data->system;
	auto& locker = fileWatcherSystem->getLocker_();
	auto& changedFiles = fileWatcherSystem->getChangedFiles();
	auto& createdFiles = fileWatcherSystem->getCreatedFiles();

	while (true)
	{
		auto status = WaitForMultipleObjects((DWORD)events.size(), events.data(), FALSE, INFINITE);
		auto index = status - WAIT_OBJECT_0;

		if (index < 0 || index >= events.size())
		{
			GARDEN_LOG_ERROR("Failed to wait for file watcher events. (error: " + to_string(GetLastError()) + ")");
			break;
		}

		auto& watcher = watchers[index];
		auto notifyInfo = (FILE_NOTIFY_INFORMATION*)watcher.buffer.data();

		locker.lock();
		do
		{
			auto filePathLength = WideCharToMultiByte(CP_UTF8, 0, notifyInfo->FileName, 
				(int)notifyInfo->FileNameLength / sizeof(WCHAR), NULL, 0, NULL, NULL);
			std::string filePath(filePathLength, 0);
			WideCharToMultiByte(CP_UTF8, 0, notifyInfo->FileName, (int)notifyInfo->FileNameLength / 
				sizeof(WCHAR), filePath.data(), filePathLength, NULL, NULL);

			if (notifyInfo->Action == FILE_ACTION_MODIFIED)
				changedFiles.emplace(watcher.path / filePath);
			else if (notifyInfo->Action == FILE_ACTION_ADDED)
				createdFiles.emplace(watcher.path / filePath);
			else continue; // TODO: moved and renamed files

			notifyInfo = (FILE_NOTIFY_INFORMATION*)((char*)notifyInfo + notifyInfo->NextEntryOffset);
		} while (notifyInfo->NextEntryOffset != 0);
		locker.unlock();

		ResetEvent(watcher.overlapped.hEvent);
		if (!enqueueDirWatcher(watcher))
			break;
	}
}
#endif

//**********************************************************************************************************************
FileWatcherSystem::FileWatcherSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("FileChange");
	manager->registerEvent("FileCreate");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", FileWatcherSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", FileWatcherSystem::update);
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
#elif GARDEN_OS_WINDOWS
static bool createDirWatcher(const fs::path& path, WatcherDir& watcher)
{
	watcher.dirHandle = CreateFileW(path.generic_wstring().c_str(), FILE_LIST_DIRECTORY, 
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	if (watcher.dirHandle == INVALID_HANDLE_VALUE)
	{
		GARDEN_LOG_ERROR("Failed to create a directory watcher. (error: " + to_string(GetLastError()) + ")");
		return false;
	}

	watcher.overlapped = {};
	watcher.overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	if (!watcher.overlapped.hEvent)
	{
		CloseHandle(watcher.dirHandle);
		GARDEN_LOG_ERROR("Failed to create a directory watcher event. (error: " + to_string(GetLastError()) + ")");
		return false;
	}

	watcher.buffer.resize(1024);
	watcher.path = path;
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
		GARDEN_LOG_ERROR("Failed to initialize inotify. (error: " + to_string(errno) + ")");
		return;
	}
	instance = (void*)(size_t)fd;

	addDirWatchers(fd, GARDEN_RESOURCES_PATH, watchers);
	addDirWatchers(fd, appResourcesPath, watchers);
	#elif GARDEN_OS_APPLE
	CFStringRef paths[2] =
	{
		CFStringCreateWithCString(kCFAllocatorDefault, 
			GARDEN_RESOURCES_PATH.c_str(), kCFStringEncodingUTF8),
		CFStringCreateWithCString(kCFAllocatorDefault, 
			appResourcesPath.c_str(), kCFStringEncodingUTF8)
	};
	auto pathsToWatch = CFArrayCreate(nullptr, (const void**)paths, 2, nullptr);

	FSEventStreamContext context;
	context.version = 0;
	context.info = this;
	context.retain = nullptr;
	context.release = nullptr;
	context.copyDescription = nullptr;

	auto stream = FSEventStreamCreate(nullptr, &onChange, &context, pathsToWatch,
		kFSEventStreamEventIdSinceNow, 1.0, kFSEventStreamCreateFlagFileEvents);
	this->instance = stream;

	auto queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	FSEventStreamSetDispatchQueue(stream, queue);
	if (!FSEventStreamStart(stream))
		GARDEN_LOG_ERROR("Failed to start FSEventStream.");
	CFRelease(pathsToWatch); CFRelease(paths[1]); CFRelease(paths[0]);
	#elif GARDEN_OS_WINDOWS
	auto data = new WatcherData();
	this->instance = data;

	data->system = this;
	data->watchers.resize(2);

	if (!createDirWatcher(GARDEN_RESOURCES_PATH, data->watchers[0]) ||
		!createDirWatcher(appResourcesPath, data->watchers[1]))
	{
		GARDEN_LOG_ERROR("Failed to create directory watchers.");
		return;
	}

	data->thread = thread(fileWatcherThread, data);
	data->thread.detach();
	#endif
}

//**********************************************************************************************************************
void FileWatcherSystem::update()
{
	#if GARDEN_OS_APPLE | GARDEN_OS_WINDOWS
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

	#if GARDEN_OS_APPLE | GARDEN_OS_WINDOWS
	locker.unlock();
	#endif
}
#endif
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

#include "garden/thread-pool.hpp"
#include "garden/defines.hpp"
#include "garden/profiler.hpp"

#include "mpmt/thread.hpp"
#include <cmath>

using namespace garden;

// TODO: maybe add queue for each thread to reduce sync count.
// This is useful if we have a lot of tasks.

//**********************************************************************************************************************
void ThreadPool::threadFunction(uint32 index)
{
	auto threadName = name.empty() ? "" : name;
	threadName += "#"; threadName += to_string(index);
	mpmt::Thread::setName(threadName.c_str());

	if (background)
		mpmt::Thread::setBackgroundPriority();
	else
		mpmt::Thread::setForegroundPriority();

	while (isRunning)
	{
		unique_lock locker(mutex);
		workCond.wait(locker, [this]()
		{
			return !tasks.empty() || !isRunning;
		});

		if (!isRunning)
			return;

		workingCount++;
		auto task = tasks.front();
		tasks.pop();

		#if GARDEN_TRACY_PROFILER
		TracyFiberEnterHint(threadName.c_str(), index);
		#endif

		locker.unlock();
		task.threadIndex = index;
		task.function(task);
		locker.lock();

		#if GARDEN_TRACY_PROFILER
		TracyFiberLeave;
		#endif

		workingCount--;
		workingCond.notify_all();
	}
}

//**********************************************************************************************************************
ThreadPool::ThreadPool(bool isBackground, const string& name, uint32 threadCount) : 
	name(name), background(isBackground), isRunning(true)
{
	GARDEN_ASSERT(threadCount > 0);
	
	if (threadCount == UINT32_MAX)
		threadCount = thread::hardware_concurrency();

	this->threads.resize(threadCount);

	for (uint32 i = 0; i < threadCount; i++)
		this->threads[i] = thread(&ThreadPool::threadFunction, this, i);
}
ThreadPool::~ThreadPool()
{
	mutex.lock();
	isRunning = false;
	mutex.unlock();
	workCond.notify_all();

	for (auto& thread : threads)
		thread.join();
}

uint32 ThreadPool::getPendingTaskCount()
{
	mutex.lock();
	auto count = (uint32)tasks.size();
	mutex.unlock();
	return count;
}

//**********************************************************************************************************************
void ThreadPool::addTask(const Task& task)
{
	GARDEN_ASSERT(task.function);
	mutex.lock();
	tasks.push(task);
	mutex.unlock();
	workCond.notify_one();
}
void ThreadPool::addTasks(const vector<Task>& tasks)
{
	GARDEN_ASSERT(!tasks.empty());

	mutex.lock();
	for (uint32 i = 0; i < (uint32)tasks.size(); i++)
	{
		auto task = tasks[i];
		GARDEN_ASSERT(task.function);
		task.taskIndex = i;
		this->tasks.push(task);
	}
	mutex.unlock();

	if (tasks.size() > 1)
		workCond.notify_all();
	else
		workCond.notify_one();
}
void ThreadPool::addTasks(const Task& task, uint32 count)
{
	GARDEN_ASSERT(task.function);
	GARDEN_ASSERT(count != 0);
	auto _task = task;

	mutex.lock();
	for (uint32 i = 0; i < count; i++)
	{
		_task.taskIndex = i;
		tasks.push(_task); 
	}
	mutex.unlock();

	if (count > 1)
		workCond.notify_all();
	else
		workCond.notify_one();
	
}
void ThreadPool::addItems(const Task& task, uint32 count)
{
	GARDEN_ASSERT(task.function);
	GARDEN_ASSERT(count != 0);
	auto _task = task;

	auto taskCount = count > threads.size() ? (uint32)threads.size() : count;
	auto countPerThread = (uint32)std::ceil((float)count / (float)taskCount);

	mutex.lock();
	for (uint32 i = 0; i < taskCount; i++)
	{
		_task.taskIndex = i;
		_task.itemOffset = countPerThread * i;
		_task.itemCount = std::min(count, _task.itemOffset + countPerThread);
		tasks.push(_task); 
	}
	mutex.unlock();

	if (taskCount > 1)
		workCond.notify_all();
	else
		workCond.notify_one();
}

//**********************************************************************************************************************
void ThreadPool::wait()
{
	auto locker = unique_lock(mutex);
	while (!tasks.empty() || workingCount > 0)
		workingCond.wait(locker);
	locker.unlock();
}
void ThreadPool::removeAll()
{
	mutex.lock();
	tasks = {};
	mutex.unlock();
}
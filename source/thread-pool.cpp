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

#include "garden/thread-pool.hpp"
#include "garden/defines.hpp"
#include "garden/thread.hpp"
#include <cmath>

using namespace garden;

// TODO: maybe add queue for each thread to reduce sync count.
// This is usefull if we have alot of tasks.

//--------------------------------------------------------------------------------------------------
void ThreadPool::threadFunction(uint32 index)
{
	if (!name.empty()) Thread::setName(name + "#" + to_string(index));
	if (background) Thread::setBackgroundPriority();
	else Thread::setForegroundPriority();

	auto locker = unique_lock(mutex);

	while (isRunning)
	{
		while (tasks.empty())
		{
			if (!isRunning)
			{
				locker.unlock();
				return;
			}

			workCond.wait(locker);
		}

		workingCount++;
		auto task = tasks.front();
		tasks.pop();

		locker.unlock();
		task.threadIndex = index;
		task.function(task);
		locker.lock();

		workingCount--;
		workingCond.notify_all();
	}

	locker.unlock();
}

//--------------------------------------------------------------------------------------------------
ThreadPool::ThreadPool(bool isBackground, const string& name, uint32 threadCount)
{
	GARDEN_ASSERT(threadCount > 0);
	
	if (threadCount == UINT32_MAX)
		threadCount = thread::hardware_concurrency();
	this->threads.resize(threadCount);

	this->name = name;
	this->background = isBackground;
	this->isRunning = true;

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

uint32 ThreadPool::getTaskCount()
{
	mutex.lock();
	auto count = (uint32)tasks.size();
	mutex.unlock();
	return count;
}

//--------------------------------------------------------------------------------------------------
void ThreadPool::addTask(const Task& task)
{
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
		task.taskIndex = i;
		this->tasks.push(task);
	}

	mutex.unlock();
	if (tasks.size() > 1) workCond.notify_all();
	else workCond.notify_one();
}
void ThreadPool::addTasks(const Task& task, uint32 count)
{
	GARDEN_ASSERT(count != 0);
	auto _task = task;
	mutex.lock();

	for (uint32 i = 0; i < count; i++)
	{
		_task.taskIndex = i;
		tasks.push(_task); 
	}

	mutex.unlock();
	if (count > 1) workCond.notify_all();
	else workCond.notify_one();
	
}
void ThreadPool::addItems(const Task& task, uint32 count)
{
	GARDEN_ASSERT(count != 0);
	auto _task = task;
	_task.itemCount = count;
	mutex.lock();

	auto threadCount = (uint32)threads.size();
	auto taskCount = (uint32)std::ceil((float)count / (float)threadCount);
	if (taskCount > threadCount) taskCount = threadCount;

	for (uint32 i = 0; i < taskCount; i++)
	{
		_task.taskIndex = i;
		_task.itemOffset = _task.itemCount * i;
		tasks.push(_task); 
	}

	mutex.unlock();
	if (taskCount > 1) workCond.notify_all();
	else workCond.notify_one();
}

//--------------------------------------------------------------------------------------------------
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
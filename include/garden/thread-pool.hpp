//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "math/types.hpp"

#include <queue>
#include <mutex>
#include <vector>
#include <thread>
#include <string>
#include <functional>
#include <condition_variable>

namespace garden
{

using namespace std;
using namespace math;

//--------------------------------------------------------------------------------------------------
class ThreadPool final
{
public:
	class Task final
	{
		std::function<void(const Task& task)> function;
		void* argument = nullptr;
		uint32 threadIndex = 0;
		uint32 taskIndex = 0;
		uint32 itemOffset = 0;
		uint32 itemCount = 0;
		friend class ThreadPool;
	public:
		Task(const std::function<void(const Task& task)>& function,
			void* argument = nullptr) noexcept
		{
			this->function = function;
			this->argument = argument;
		}

		std::function<void(const Task& task)> getFunction()
			const noexcept { return function; }
		void* getArgument() const noexcept { return argument; }

		uint32 getThreadIndex() const noexcept { return threadIndex; }
		uint32 getTaskIndex() const noexcept { return taskIndex; }
		uint32 getItemOffset() const noexcept { return itemOffset; }
		uint32 getItemCount() const noexcept { return itemCount; }
	};
private:
	string name;
	std::mutex mutex = {};
	condition_variable workCond = {};
	condition_variable workingCond = {};
	vector<thread> threads;
	queue<Task> tasks;
	uint32 workingCount = 0;
	bool background = false;
	bool isRunning = false;

	void threadFunction(uint32 index);
public:
	ThreadPool(bool isBackground = false,
		const string& name = "", uint32 threadCount = UINT32_MAX);
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	~ThreadPool();

	const string& getName() const noexcept { return name; }
	uint32 getThreadCount() const noexcept { return (uint32)threads.size(); }
	bool isBackground() const noexcept { return background; }
	uint32 getTaskCount();

	void addTask(const Task& task);
	void addTasks(const vector<Task>& tasks);
	void addTasks(const Task& task, uint32 count);
	void addItems(const Task& task, uint32 count);

	void wait();
	void removeAll();
};

} // garden
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

/***********************************************************************************************************************
 * @file
 * @brief Common thread pool functions.
 */

#pragma once
#include "garden/defines.hpp"

#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include <condition_variable>

namespace garden
{

/***********************************************************************************************************************
 * @brief Asynchronous task executor.
 * 
 * @details
 * A thread pool is a design pattern used particularly in concurrent programming, to manage a group of threads 
 * efficiently. Instead of creating a new thread each time a task needs to be executed, a thread pool pre-creates a 
 * fixed number of threads at startup and maintains a queue of tasks. When a task is submitted to the thread pool, 
 * it's assigned to one of the available threads. Once the task is completed, the thread becomes available to 
 * execute another task from the queue.
 */
class ThreadPool final
{
public:
	/*******************************************************************************************************************
	 * @brief Task is a unit of work that needs to be performed asynchronously.
	 */
	class Task final
	{
	public:
		using Function = std::function<void(const Task& task)>;
	private:
		Function function = {};
		float priority = 0.0f;
		uint32 threadIndex = 0;
		uint32 taskIndex = 0;
		uint32 itemOffset = 0;
		uint32 itemCount = 0;

		template<class T>
		Task(const T& function, float priority = 0.0f) noexcept : 
			function(function), priority(priority) { }

		friend class ThreadPool;
	public:
		/**
		 * @brief Creates a new empty task data container.
		 */
		Task() = default;
		
		/**
		 * @brief Returns function that should be executed by a thread
		 * @details This function is mainly useful for debugging purposes.
		 */
		Function getFunction() const noexcept { return function; }
		/**
		 * @brief Returns task execution priority in the pool.
		 * @details You can use this inside a task function.
		 */
		float getPriority() const noexcept { return priority; }

		/**
		 * @brief Returns current thread index in the pool.
		 * @details You can use this inside a task function.
		 */
		uint32 getThreadIndex() const noexcept { return threadIndex; }
		/**
		 * @brief Returns current task index in the array.
		 * @details You can use this inside a task function.
		 */
		uint32 getTaskIndex() const noexcept { return taskIndex; }
		/**
		 * @brief Returns current item index in the array.
		 * @details You can use this inside a task function.
		 */
		uint32 getItemOffset() const noexcept { return itemOffset; }
		/**
		 * @brief Returns total item count in the array.
		 * @details You can use this inside a task function.
		 */
		uint32 getItemCount() const noexcept { return itemCount; }
	};
private:
	using TaskQueue = multimap<float, Task, std::greater<float>>;

	string name;
	std::mutex mutex = {};
	condition_variable workCond = {};
	condition_variable workingCond = {};
	vector<thread> threads;
	TaskQueue taskQueue;
	uint32 workingCount = 0;
	bool background = false;
	bool isRunning = false;

	void threadFunction(uint32 index);
public:
	/*******************************************************************************************************************
	 * @brief Creates a new thread pool.
	 * 
	 * @param isBackground set background threads priority
	 * @param[in] name thread pool thread name
	 * @param threadCount target pool thread count
	 */
	ThreadPool(bool isBackground = false, const string& name = "", uint32 threadCount = UINT32_MAX);
	/**
	 * @brief Destroys thread pool. (Blocking)
	 * @details Waits until all running tasks are completed.
	 * @warning Pending queue tasks will be dropped!
	 */
	~ThreadPool();

	ThreadPool(ThreadPool&&) = delete;
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	/**
	 * @brief Returns threads name. (MT-Safe)
	 * @details Useful for logging current thread name. (@ref mpmt::Thread::getName())
	 */
	const string& getName() const noexcept { return name; }
	/**
	 * @brief Returns thread count in the pool. (MT-Safe)
	 * @details Optimal value is a logical CPU count in the PC.
	 */
	uint32 getThreadCount() const noexcept { return (uint32)threads.size(); }
	/**
	 * @brief Does threads have a background priority. (MT-Safe)
	 * @details This affects how threads are scheduled for execution by the scheduler.
	 */
	bool isBackground() const noexcept { return background; }
	/**
	 * @brief Returns curent task queue pending task count. (MT-Safe)
     * @details Locks mutex inside to get current task count.
	 */
	uint32 getPendingTaskCount();

	/**
	 * @brief Adds a new task to the pending task queue. (MT-Safe)
	 * @warning You should manually synchronize data access and prevent race conditions!
	 * 
	 * @param[in] function target task function
	 * @param priority task execution priority
	 */
	void addTask(const Task::Function& function, float priority = 0.0f);
	/**
	 * @brief Adds new tasks to the pending task queue. (MT-Safe)
	 * @warning You should manually synchronize data access and prevent race conditions!
	 * 
	 * @param[in] functions target task function array
	 * @param priority tasks execution priority
	 */
	void addTasks(const vector<Task::Function>& functions, float priority = 0.0f);
	/**
	 * @brief Adds a new task count to the pending task queue. (MT-Safe)
	 * @warning You should manually synchronize data access and prevent race conditions!
	 * 
	 * @param[in] task target task function
	 * @param count task instance count
	 * @param priority tasks execution priority
	 */
	void addTasks(const Task::Function& function, uint32 count, float priority = 0.0f);
	/**
	 * @brief Adds a new items to the pending task queue. (MT-Safe)
	 * @warning You should manually synchronize data access and prevent race conditions!
	 * 
	 * @details
	 * This function distributes items among all threads in the pool, and creates 
	 * a number of tasks equal to the number of threads in the pool.
	 * 
	 * @param[in] task target task function
	 * @param count target item count
	 * @param priority tasks execution priority
	 */
	void addItems(const Task::Function& task, uint32 count, float priority = 0.0f);

	/**
	 * @brief Waits until all pending in the queue and running tasks are completed. (Blocking)
	 * @details Use it wait until all running and pending tasks are completed.
	 */
	void wait();
	/**
	 * @brief Drops all pending tasks in the queue.
	 * @details Locks mutex inside to clear the queue.
	 */
	void removeAll();
	/**
	 * @brief Stops thread pool tasks execution and joins all threads.
	 */
	void stop();
};

} // namespace garden
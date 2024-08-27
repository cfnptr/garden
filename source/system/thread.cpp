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

#include "garden/system/thread.hpp"

using namespace garden;

//**********************************************************************************************************************
ThreadSystem* ThreadSystem::instance = nullptr;

ThreadSystem::ThreadSystem() : backgroundPool(true, "BG"), foregroundPool(false, "FG")
{
	mpmt::Thread::setForegroundPriority();
	SUBSCRIBE_TO_EVENT("PreDeinit", ThreadSystem::preDeinit);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
ThreadSystem::~ThreadSystem()
{
	if (Manager::get()->isRunning())
		UNSUBSCRIBE_FROM_EVENT("PreDeinit", ThreadSystem::preDeinit);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void ThreadSystem::preDeinit()
{
	backgroundPool.removeAll();
	backgroundPool.wait();
}
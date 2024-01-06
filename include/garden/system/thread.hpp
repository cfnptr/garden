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
#include "ecsm.hpp"
#include "garden/thread-pool.hpp"

namespace garden
{

using namespace std;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
class ThreadSystem final : public System
{
	ThreadPool backgroundPool;
	ThreadPool foregroundPool;

	ThreadSystem() : backgroundPool(true, "BG"),
		foregroundPool(false, "FG") { }
		
	void terminate() final
	{
		backgroundPool.removeAll();
		backgroundPool.wait();
	}

	friend class ecsm::Manager;
public:
	ThreadPool& getBackgroundPool() noexcept { return backgroundPool; }
	ThreadPool& getForegroundPool() noexcept { return foregroundPool; }
};

} // garden
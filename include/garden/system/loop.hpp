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

/***********************************************************************************************************************
 * @file
 * @brief Update loop functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief Handles game or server manual loop.
 */
class LoopSystem final : public System, public Singleton<LoopSystem>
{
	double currentTime = 0.0, systemTime = 0.0, deltaTime = 0.0;

	/**
	 * @brief Creates a new loop system instance.
	 * @param setSingleton set system singleton instance
	 */
	LoopSystem(bool setSingleton = true);
	/**
	 * @brief Destroys loop system instance.
	 */
	~LoopSystem() final;

	void preInit();
	void deinit();
	void input();
	void output();
	
	friend class ecsm::Manager;
public:
	/**
	 * @brief Current time multiplier.
	 * @details Can be used to simulate slow motion or speed up effects.
	 */
	double timeMultiplier = 1.0;
	/**
	 * @brief Maximum update ticks per second count.
	 * @details Limits system update count per second.
	 */
	uint16 maxTickRate = 60;

	/**
	 * @brief Returns time since start of the program. (in seconds)
	 * @details You can use it to implement time based events or delays.
	 * @note It is affected by the timeMultiplier value.
	 */
	double getCurrentTime() const noexcept { return currentTime; }
	/**
	 * @brief Returns current system time. (in seconds)
	 * @note It is NOT affected by the timeMultiplier value.
	 */
	double getSystemTime() const noexcept { return systemTime; }
	/**
	 * @brief Returns time elapsed between two previous ticks. (in seconds)
	 * @note It is affected by the timeMultiplier value.
	 * 
	 * @details
	 * This value is crucial for ensuring that animations, physics calculations, 
	 * and game logic run smoothly and consistently, regardless of the tick rate.
	 */
	double getDeltaTime() const noexcept { return deltaTime; }
};

} // namespace garden
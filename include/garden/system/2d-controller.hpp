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
 * @brief Common 2D view controller functions
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "math/vector.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief General 2D view controller.
 */
class Controller2DSystem final : public System, public Singleton<Controller2DSystem>
{
	ID<Entity> camera = {};
	float2 followTarget = float2(0.0f);
	bool canDoubleJump = true;
	bool isLastJumping = false;

	/**
	 * @brief Creates a new 2D view controller system instance.
	 * @param setSingleton set system singleton instance
	 */
	Controller2DSystem(bool setSingleton = true);
	/**
	 * @brief Destroys 2D view controller system instance.
	 */
	~Controller2DSystem() final;

	void updateCameraControll();
	void updateCameraFollowing();
	void updateCharacterControll();

	void init();
	void deinit();
	void update();
	void swapchainRecreate();
	
	friend class ecsm::Manager;
public:
	string characterEntityTag = "MainCharacter";
	float scrollSensitivity = 1.0f;
	float horizontalSpeed = 2.0f;
	float horizontalFactor = 0.99999f;
	float jumpSpeed = 4.0f;
	float followThreshold = 0.6f;
	float followLerpFactor = 0.8f;
	float2 followCenter = float2(0.0f, 0.25f);
	bool useMouseControll = GARDEN_DEBUG ? true : false;
	bool useDoubleJump = true;

	bool isDoubleJumped() const noexcept { return !canDoubleJump; }
};

} // namespace garden
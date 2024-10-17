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

/***********************************************************************************************************************
 * @file
 * @brief Common first person view controller functions
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "math/quaternion.hpp"

namespace garden
{

using namespace ecsm;

/***********************************************************************************************************************
 * @brief General first person view controller.
 */
class FpvControllerSystem final : public System, public Singleton<FpvControllerSystem>
{
	ID<Entity> camera = {};
	float2 rotation = float2(0.0f);
	float3 velocity = float3(0.0f);
	bool isLastJumping = false;

	/**
	 * @brief Creates a new first person view controller system instance.
	 * @param setSingleton set system singleton instance
	 */
	FpvControllerSystem(bool setSingleton = true);
	/**
	 * @brief Destroys first person view controller system instance.
	 */
	~FpvControllerSystem() final;

	quat updateCameraRotation();
	void updateCameraControll(const quat& rotationQuat);
	void updateCharacterControll();

	void init();
	void deinit();
	void update();
	void swapchainRecreate();
	
	friend class ecsm::Manager;
public:
	string characterEntityTag = "MainCharacter";
	float mouseSensitivity = 1.0f;
	float moveSpeed = 1.0f;
	float moveLerpFactor = 20.0f;
	float boostFactor = 10.0f;
	float horizontalSpeed = 2.0f;
	float horizontalFactor = 0.99999f;
	float jumpSpeed = 4.0f;
};

} // namespace garden
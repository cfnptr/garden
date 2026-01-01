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
 * @brief Common first person view controller functions.
 */

#pragma once
#include "garden/system/character.hpp"

namespace garden
{

using namespace ecsm;

/**
 * @brief General first person view controller.
 */
class FpvControllerSystem final : public System, public Singleton<FpvControllerSystem>
{
	ID<Entity> camera = {};
	float boostAccum = 1.0f;
	f32x4 velocity = f32x4::zero;

	/**
	 * @brief Creates a new first person view controller system instance.
	 * @param setSingleton set system singleton instance
	 */
	FpvControllerSystem(bool setSingleton = true);
	/**
	 * @brief Destroys first person view controller system instance.
	 */
	~FpvControllerSystem() final;

	void updateMouseLock();
	quat updateCameraRotation();
	void updateCameraControl(quat rotationQuat);
	void updateCharacterControl();

	void init();
	void deinit();
	void update();
	void swapchainRecreate();
	
	friend class ecsm::Manager;
public:
	CharacterComponent::UpdateSettings updateSettings;
	string characterEntityTag = "MainCharacter";
	float2 rotation = float2::zero;
	float mouseSensitivity = 1.0f;
	float moveSpeed = 2.0f;
	float moveLerpFactor = 0.99999f;
	float boostFactor = 2.0f;
	float jumpSpeed = 5.0f;
	float swimSpeed = 1.0f;
	float swimpResist = 0.9f;
	bool isMouseLocked = false;
	bool canSwim = false;
};

} // namespace garden
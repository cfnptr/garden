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
 * @brief Common 2D view controller functions
 */

#pragma once
#include "garden/defines.hpp"
#include "math/vector.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;

/***********************************************************************************************************************
 * @brief General 2D view controller.
 */
class Controller2DSystem final : public System
{
	ID<Entity> camera = {};
	bool isDragging = false;

	/**
	 * @brief Creates a new 2D view controller system instance.
	 */
	Controller2DSystem();
	/**
	 * @brief Destroys 2D view controller system instance.
	 */
	~Controller2DSystem() final;

	void init();
	void deinit();
	void update();
	void swapchainRecreate();

	friend class ecsm::Manager;
public:
	float scrollSensitivity = 1.0f;
};

} // namespace garden
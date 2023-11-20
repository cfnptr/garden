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

#pragma once
#include "garden/system/physics.hpp"
#include "garden/system/graphics.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace garden;
using namespace garden::physics;

//--------------------------------------------------------------------------------------------------
class FpvSystem final : public System, public IPhysicsSystem
{
	GraphicsSystem* graphicsSystem = nullptr;
	float2 lastCursorPosition = float2(0.0f);
	float2 rotation = float2(0.0f);
	float3 velocity = float3(0.0f);
	ID<Material> material = {};
	int32 triggerCount = 0;
public:
	float viewSensitivity = 1.0f;
	float moveSensitivity = 1.0f;
	float moveBoost = 10.0f;
	float lerpMultiplier = 20.0f;
	float jumpDelay = 0.15f;
private:
	double lastJumpTime = 0.0f;
	bool lastTabState = false;
	bool lastF10State = false;

	void initialize() final;
	void update() final;

	void onTrigger(const TriggerData& data) final;
	void postSimulate() final;
	
	friend class ecsm::Manager;
};

} // garden
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
#include "garden/system/fpv.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/graphics/geometry/opaque.hpp"
#include "garden/system/graphics/geometry/cutoff.hpp"
#include "garden/system/graphics/geometry/translucent.hpp"

namespace garden
{

using namespace ecsm;
using namespace garden;

//--------------------------------------------------------------------------------------------------
class DemoSystem final : public System
{
	bool isLoaded = false;

	void initialize() final;
	void update() final;
	
	friend class ecsm::Manager;
};

} // garden
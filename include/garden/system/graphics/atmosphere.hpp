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
#include "garden/system/graphics.hpp"

// TODO: procedural atmosphere for planet and sky fligts.
// https://ebruneton.github.io/precomputed_atmospheric_scattering/

namespace garden
{

using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// Physically Based Atmosphere
class AtmosphereRenderSystem final : public System, public IRenderSystem
{
	void initialize() final;	
	void render() final;
	
	friend class ecsm::Manager;
};

} // garden
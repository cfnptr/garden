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

#include "garden/defines.hpp" // TODO: tmp
#undef GARDEN_EDITOR // TODO: tmp
#define GARDEN_EDITOR 0 // TODO: tmp

#include "garden/main.hpp"
#include "garden/system/log.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/transform.hpp"

using namespace ecsm;
using namespace garden;

void entryPoint()
{
	auto manager = new Manager();
	manager->createSystem<DoNotDestroySystem>();
	manager->createSystem<BakedTransformSystem>();
	manager->createSystem<SettingsSystem>();
	manager->createSystem<LogSystem>();
	manager->createSystem<CameraSystem>();
	manager->createSystem<TransformSystem>();
	manager->createSystem<InputSystem>();
	manager->createSystem<GraphicsSystem>();
	manager->createSystem<ThreadSystem>();
	manager->initialize();
	
	auto graphicsSystem = manager->get<GraphicsSystem>();
	graphicsSystem->setWindowTitle("Platformer");

	manager->start();
	delete manager;
}

GARDEN_DECLARE_MAIN(entryPoint)
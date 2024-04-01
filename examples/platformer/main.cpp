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

#include "garden/main.hpp"
#include "garden/system/log.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/transform.hpp"
#include "platformer/defines.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#endif

using namespace ecsm;
using namespace garden;
using namespace platformer;

void entryPoint()
{
	auto manager = new Manager();
	createAppSystem(manager);
	manager->createSystem<DoNotDestroySystem>();
	manager->createSystem<LogSystem>();
	manager->createSystem<SettingsSystem>();
	manager->createSystem<ResourceSystem>();
	manager->createSystem<CameraSystem>();
	manager->createSystem<TransformSystem>();
	manager->createSystem<BakedTransformSystem>();
	manager->createSystem<InputSystem>();
	manager->createSystem<GraphicsSystem>();
	manager->createSystem<ThreadSystem>();
	#if GARDEN_EDITOR
	manager->createSystem<EditorRenderSystem>();
	#endif
	manager->initialize();
	
	GraphicsSystem::getInstance()->setWindowTitle("Platformer");

	manager->start();
	delete manager;
}

GARDEN_DECLARE_MAIN(entryPoint)
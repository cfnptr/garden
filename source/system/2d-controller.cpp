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

#include "garden/system/2d-controller.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/camera.hpp"

using namespace garden;

//**********************************************************************************************************************
Controller2DSystem::Controller2DSystem(Manager* manager) : System(manager)
{
	SUBSCRIBE_TO_EVENT("Init", Controller2DSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", Controller2DSystem::deinit);
	SUBSCRIBE_TO_EVENT("Update", Controller2DSystem::update);
}
Controller2DSystem::~Controller2DSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", Controller2DSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", Controller2DSystem::deinit);
		UNSUBSCRIBE_FROM_EVENT("Update", Controller2DSystem::update);
	}
}

void Controller2DSystem::init()
{
	auto manager = getManager();
	GARDEN_ASSERT(manager->has<TransformSystem>());
	GARDEN_ASSERT(manager->has<CameraSystem>());
	GARDEN_ASSERT(manager->has<GraphicsSystem>());

	camera = manager->createEntity();

	auto transformComponent = manager->add<TransformComponent>(camera);
	transformComponent->position = float3(0.0f, 0.0f, -0.5f);

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto windowSize = graphicsSystem->getWindowSize();
	auto aspectRatio = (float)windowSize.x / (float)windowSize.y;
	const auto defaultSize = 5.0f;

	auto cameraComponent = manager->add<CameraComponent>(camera);
	cameraComponent->type = ProjectionType::Orthographic;
	cameraComponent->p.orthographic.depth = float2(0.0f, 1.0f);
	cameraComponent->p.orthographic.width = float2(-defaultSize, defaultSize) * aspectRatio;
	cameraComponent->p.orthographic.height = float2(-defaultSize, defaultSize);

	GARDEN_ASSERT(!graphicsSystem->camera) // Several main cameras detected!
	graphicsSystem->camera = camera;
}
void Controller2DSystem::deinit()
{
	auto manager = getManager();
	if (manager->isRunning())
		manager->destroy(camera);
}

//**********************************************************************************************************************
void Controller2DSystem::update()
{
	
}
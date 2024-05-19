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

#if GARDEN_EDITOR
#include "garden/editor/system/render/infinite-grid.hpp"
#endif

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

	SUBSCRIBE_TO_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);

	camera = manager->createEntity();
	manager->add<DoNotDestroyComponent>(camera);

	auto transformComponent = manager->add<TransformComponent>(camera);
	transformComponent->position = float3(0.0f, 0.0f, -0.5f);
	#if GARDEN_DEBUG | GARDEN_EDITOR
	transformComponent->name = "Camera";
	#endif

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto windowSize = graphicsSystem->getWindowSize();
	auto aspectRatio = (float)windowSize.x / (float)windowSize.y;
	const auto defaultSize = 2.0f;

	auto cameraComponent = manager->add<CameraComponent>(camera);
	cameraComponent->type = ProjectionType::Orthographic;
	cameraComponent->p.orthographic.depth = float2(0.0f, 1.0f);
	cameraComponent->p.orthographic.width = float2(-defaultSize, defaultSize) * aspectRatio;
	cameraComponent->p.orthographic.height = float2(-defaultSize, defaultSize);

	GARDEN_ASSERT(!graphicsSystem->camera) // Several main cameras detected!
	graphicsSystem->camera = camera;

	#if GARDEN_EDITOR
	auto infiniteGrid = manager->tryGet<InfiniteGridEditorSystem>();
	if (infiniteGrid)
		infiniteGrid->isHorizontal = false;
	#endif
}
void Controller2DSystem::deinit()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		auto graphicsSystem = GraphicsSystem::getInstance();
		graphicsSystem->camera = {};
		manager->destroy(camera);

		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void Controller2DSystem::update()
{
	auto inputSystem = InputSystem::getInstance();
	if (ImGui::GetIO().WantCaptureMouse || inputSystem->getCursorMode() != CursorMode::Default)
		return;

	if (inputSystem->getMouseState(MouseButton::Right))
	{
		auto manager = getManager();
		auto graphicsSystem = GraphicsSystem::getInstance();
		auto cameraComponent = manager->get<CameraComponent>(camera);
		auto transformComponent = manager->get<TransformComponent>(camera);
		auto cursorDelta = inputSystem->getCursorDelta();
		auto windowSize = (float2)graphicsSystem->getWindowSize();
		auto othoSize = float2(
			cameraComponent->p.orthographic.width.y - cameraComponent->p.orthographic.width.x,
			cameraComponent->p.orthographic.height.y - cameraComponent->p.orthographic.height.x);
		auto offset = cursorDelta / (windowSize / othoSize);
		transformComponent->position.x -= offset.x;
		transformComponent->position.y += offset.y;
	}

	auto mouseScroll = inputSystem->getMouseScroll();
	if (mouseScroll != float2(0.0f))
	{
		mouseScroll *= scrollSensitivity * 0.5f;

		auto manager = getManager();
		auto graphicsSystem = GraphicsSystem::getInstance();
		auto framebufferSize = graphicsSystem->getFramebufferSize();
		auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		auto cameraComponent = manager->get<CameraComponent>(camera);
		cameraComponent->p.orthographic.height.x += mouseScroll.y;
		cameraComponent->p.orthographic.height.y -= mouseScroll.y;
		if (cameraComponent->p.orthographic.height.x >= 0.0f)
			cameraComponent->p.orthographic.height.x = -0.1f;
		if (cameraComponent->p.orthographic.height.y <= 0.0f)
			cameraComponent->p.orthographic.height.y = 0.1f;
		cameraComponent->p.orthographic.width = cameraComponent->p.orthographic.height * aspectRatio;
	}
}

void Controller2DSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto framebufferSize = graphicsSystem->getFramebufferSize();
		auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		auto cameraComponent = getManager()->get<CameraComponent>(camera);
		cameraComponent->p.orthographic.width = cameraComponent->p.orthographic.height * aspectRatio;
	}
}
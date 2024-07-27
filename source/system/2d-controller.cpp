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
Controller2DSystem::Controller2DSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", Controller2DSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", Controller2DSystem::deinit);
	SUBSCRIBE_TO_EVENT("Update", Controller2DSystem::update);
}
Controller2DSystem::~Controller2DSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", Controller2DSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", Controller2DSystem::deinit);
		UNSUBSCRIBE_FROM_EVENT("Update", Controller2DSystem::update);
	}
}

void Controller2DSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<TransformSystem>());
	GARDEN_ASSERT(manager->has<CameraSystem>());
	GARDEN_ASSERT(manager->has<GraphicsSystem>());

	SUBSCRIBE_TO_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);

	camera = manager->createEntity();
	manager->add<DoNotDestroyComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->position = float3(0.0f, 0.0f, -0.5f);
	#if GARDEN_DEBUG | GARDEN_EDITOR
	transformView->debugName = "Camera";
	#endif

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto windowSize = graphicsSystem->getWindowSize();
	auto aspectRatio = (float)windowSize.x / (float)windowSize.y;
	const auto defaultSize = 2.0f;

	auto cameraView = manager->add<CameraComponent>(camera);
	cameraView->type = ProjectionType::Orthographic;
	cameraView->p.orthographic.depth = float2(0.0f, 1.0f);
	cameraView->p.orthographic.width = float2(-defaultSize, defaultSize) * aspectRatio;
	cameraView->p.orthographic.height = float2(-defaultSize, defaultSize);

	GARDEN_ASSERT(!graphicsSystem->camera) // Several main cameras detected!
	graphicsSystem->camera = camera;

	#if GARDEN_EDITOR
	auto infiniteGridSystem = manager->tryGet<InfiniteGridEditorSystem>();
	if (infiniteGridSystem)
		infiniteGridSystem->isHorizontal = false;
	#endif
}
void Controller2DSystem::deinit()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		GraphicsSystem::getInstance()->camera = {};
		manager->destroy(camera);

		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void Controller2DSystem::update()
{
	auto manager = Manager::getInstance();
	auto inputSystem = InputSystem::getInstance();
	if (ImGui::GetIO().WantCaptureMouse || inputSystem->getCursorMode() != CursorMode::Default ||
		!manager->has<TransformComponent>(camera) || !manager->has<CameraComponent>(camera))
	{
		return;
	}

	if (inputSystem->getMouseState(MouseButton::Right))
	{
		
		auto cameraView = manager->get<CameraComponent>(camera);
		auto transformView = TransformSystem::getInstance()->get(camera);
		auto cursorDelta = inputSystem->getCursorDelta();
		auto windowSize = (float2)GraphicsSystem::getInstance()->getWindowSize();
		auto othoSize = float2(
			cameraView->p.orthographic.width.y - cameraView->p.orthographic.width.x,
			cameraView->p.orthographic.height.y - cameraView->p.orthographic.height.x);
		auto offset = cursorDelta / (windowSize / othoSize);
		offset = (float2x2)transformView->calcModel() * offset;

		transformView->position.x -= offset.x;
		transformView->position.y += offset.y;
	}

	auto mouseScroll = inputSystem->getMouseScroll();
	if (mouseScroll != float2(0.0f))
	{
		mouseScroll *= scrollSensitivity * 0.5f;

		auto manager = Manager::getInstance();
		auto framebufferSize = (float2)GraphicsSystem::getInstance()->getFramebufferSize();
		auto aspectRatio = framebufferSize.x / framebufferSize.y;
		auto cameraView = manager->get<CameraComponent>(camera);
		cameraView->p.orthographic.height.x += mouseScroll.y;
		cameraView->p.orthographic.height.y -= mouseScroll.y;
		if (cameraView->p.orthographic.height.x >= 0.0f)
			cameraView->p.orthographic.height.x = -0.1f;
		if (cameraView->p.orthographic.height.y <= 0.0f)
			cameraView->p.orthographic.height.y = 0.1f;
		cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
	}
}

void Controller2DSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto framebufferSize = graphicsSystem->getFramebufferSize();
		auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		auto cameraView = Manager::getInstance()->get<CameraComponent>(camera);
		cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
	}
}
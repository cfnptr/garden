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
#include "garden/system/physics.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/link.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/infinite-grid.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
Controller2DSystem::Controller2DSystem()
{
	SUBSCRIBE_TO_EVENT("Init", Controller2DSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", Controller2DSystem::deinit);
	SUBSCRIBE_TO_EVENT("Update", Controller2DSystem::update);
}
Controller2DSystem::~Controller2DSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", Controller2DSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", Controller2DSystem::deinit);
		UNSUBSCRIBE_FROM_EVENT("Update", Controller2DSystem::update);
	}
}

void Controller2DSystem::init()
{
	SUBSCRIBE_TO_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);

	auto manager = Manager::get();
	camera = manager->createEntity();
	manager->add<DoNotDestroyComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->position = float3(0.0f, 0.0f, -0.5f);
	#if GARDEN_DEBUG | GARDEN_EDITOR
	transformView->debugName = "MainCamera";
	#endif

	auto linkView = manager->add<LinkComponent>(camera);
	linkView->setTag("MainCamera");

	auto graphicsSystem = GraphicsSystem::get();
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
	if (Manager::get()->isRunning())
	{
		GraphicsSystem::get()->camera = {};
		Manager::get()->destroy(camera);

		UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
static void updateCameraTransform(ID<Entity> camera, float scrollSensitivity)
{
	auto manager = Manager::get();
	auto inputSystem = InputSystem::get();

	if (ImGui::GetIO().WantCaptureMouse || inputSystem->getCursorMode() != CursorMode::Default ||
		!manager->has<TransformComponent>(camera) || !manager->has<CameraComponent>(camera))
	{
		return;
	}

	if (inputSystem->getMouseState(MouseButton::Right))
	{
		auto cameraView = manager->get<CameraComponent>(camera);
		auto transformView = TransformSystem::get()->get(camera);
		auto cursorDelta = inputSystem->getCursorDelta();
		auto windowSize = (float2)GraphicsSystem::get()->getWindowSize();
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

		auto manager = Manager::get();
		auto framebufferSize = (float2)GraphicsSystem::get()->getFramebufferSize();
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

//**********************************************************************************************************************
static void updateCharacterControll(const string& characterEntityTag,
	float horizontalSpeed, float horizontalFactor, float jumpSpeed)
{
	#if GARDEN_EDITOR
	auto editorSystem = Manager::get()->tryGet<EditorRenderSystem>();
	if (editorSystem && !editorSystem->isPlaying())
		return;
	#endif

	auto characterEntities = LinkSystem::get()->findEntities(characterEntityTag);
	if (characterEntities.first != characterEntities.second)
	{
		auto inputSystem = InputSystem::get();
		auto transformSystem = TransformSystem::get();
		auto characterSystem = CharacterSystem::get();
		auto deltaTime = (float)inputSystem->getDeltaTime();
		auto gravity = PhysicsSystem::get()->getGravity();

		auto horizontalVelocity = 0.0f;
		if (inputSystem->getKeyboardState(KeyboardButton::A) ||
			inputSystem->getKeyboardState(KeyboardButton::Left))
		{
			horizontalVelocity = -horizontalSpeed;
		}
		else if (inputSystem->getKeyboardState(KeyboardButton::D) ||
			inputSystem->getKeyboardState(KeyboardButton::Right))
		{
			horizontalVelocity = horizontalSpeed;
		}
		auto isJumping = inputSystem->getKeyboardState(KeyboardButton::Space);

		for (auto i = characterEntities.first; i != characterEntities.second; i++)
		{
			auto characterView = characterSystem->tryGet(i->second);
			if (!characterView || !characterView->getShape())
				continue;

			auto transformView = transformSystem->get(i->second);
			if (transformView && !transformView->isActiveWithAncestors())
				continue;

			auto position = characterView->getPosition();
			if (position.z != 0.0f)
				characterView->setPosition(float3(position.x, position.y, 0.0f));

			auto linearVelocity = characterView->getLinearVelocity();
			linearVelocity.x = lerpDelta(linearVelocity.x,
				horizontalVelocity, 1.0f - horizontalFactor, deltaTime);
			if (characterView->getGroundState() == CharacterGround::OnGround)
				linearVelocity.y = isJumping ? jumpSpeed : 0.0f;
			else
				linearVelocity.y += gravity.y * deltaTime;

			characterView->setLinearVelocity(linearVelocity);
			characterView->update(deltaTime, gravity);
		}
	}
}

void Controller2DSystem::update()
{
	updateCameraTransform(camera, scrollSensitivity);
	updateCharacterControll(characterEntityTag, horizontalSpeed, horizontalFactor, jumpSpeed);
}

void Controller2DSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto framebufferSize = graphicsSystem->getFramebufferSize();
		auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		auto cameraView = Manager::get()->get<CameraComponent>(camera);
		cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
	}
}
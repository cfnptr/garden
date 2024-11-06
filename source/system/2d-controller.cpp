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
#include "garden/system/character.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/link.hpp"
#include "garden/profiler.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/render/infinite-grid.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
Controller2DSystem::Controller2DSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", Controller2DSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", Controller2DSystem::deinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", Controller2DSystem::update);
}
Controller2DSystem::~Controller2DSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", Controller2DSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", Controller2DSystem::deinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", Controller2DSystem::update);
	}

	unsetSingleton();
}

void Controller2DSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);

	auto manager = Manager::Instance::get();
	camera = manager->createEntity();
	manager->add<DoNotDestroyComponent>(camera);
	manager->add<DoNotSerializeComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->position = float3(0.0f, 0.0f, -0.5f);
	#if GARDEN_DEBUG | GARDEN_EDITOR
	transformView->debugName = "Main Camera";
	#endif

	auto linkView = manager->add<LinkComponent>(camera);
	linkView->setTag("MainCamera");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getFramebufferSize();
	auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
	const auto defaultSize = 2.0f;

	auto cameraView = manager->add<CameraComponent>(camera);
	cameraView->type = ProjectionType::Orthographic;
	cameraView->p.orthographic.depth = float2(0.0f, 1.0f);
	cameraView->p.orthographic.width = float2(-defaultSize, defaultSize) * aspectRatio;
	cameraView->p.orthographic.height = float2(-defaultSize, defaultSize);

	GARDEN_ASSERT(!graphicsSystem->camera); // Several main cameras detected!
	graphicsSystem->camera = camera;

	#if GARDEN_EDITOR
	auto infiniteGridSystem = manager->tryGet<InfiniteGridEditorSystem>();
	if (infiniteGridSystem)
		infiniteGridSystem->isHorizontal = false;
	#endif
}
void Controller2DSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsSystem::Instance::get()->camera = {};
		Manager::Instance::get()->destroy(camera);

		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void Controller2DSystem::update()
{
	SET_CPU_ZONE_SCOPED("2D Controller Update");

	if (useMouseControll)
		updateCameraControll();

	updateCharacterControll();
	updateCameraFollowing();
}

void Controller2DSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto cameraView = CameraSystem::Instance::get()->tryGetComponent(camera);
		if (cameraView)
		{
			auto framebufferSize = graphicsSystem->getFramebufferSize();
			auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
			cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
		}
	}
}

//**********************************************************************************************************************
void Controller2DSystem::updateCameraControll()
{
	auto inputSystem = InputSystem::Instance::get();

	#if GARDEN_EDITOR
	if (ImGui::GetIO().WantCaptureMouse || inputSystem->getCursorMode() != CursorMode::Default)
		return;
	#endif

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(camera);
	auto cameraView = CameraSystem::Instance::get()->tryGetComponent(camera);

	if (!transformView || !transformView->isActive() || 
		!cameraView || cameraView->type != ProjectionType::Orthographic)
	{
		return;
	}

	if (inputSystem->getMouseState(MouseButton::Right))
	{
		auto cursorDelta = inputSystem->getCursorDelta();
		auto windowSize = (float2)inputSystem->getWindowSize();
		auto othoSize = float2(
			cameraView->p.orthographic.width.y - cameraView->p.orthographic.width.x,
			cameraView->p.orthographic.height.y - cameraView->p.orthographic.height.x);
		auto offset = cursorDelta / (windowSize / othoSize);
		offset = (float2x2)transformView->calcModel() * offset;

		transformView->position.x -= offset.x;
		transformView->position.y += offset.y;
	}

	auto mouseScrollY = inputSystem->getMouseScroll().y;
	if (mouseScrollY != 0.0f)
	{
		mouseScrollY *= scrollSensitivity * 0.5f;

		auto framebufferSize = (float2)GraphicsSystem::Instance::get()->getFramebufferSize();
		auto aspectRatio = framebufferSize.x / framebufferSize.y;
		cameraView->p.orthographic.height.x += mouseScrollY;
		cameraView->p.orthographic.height.y -= mouseScrollY;
		if (cameraView->p.orthographic.height.x >= 0.0f)
			cameraView->p.orthographic.height.x = -0.1f;
		if (cameraView->p.orthographic.height.y <= 0.0f)
			cameraView->p.orthographic.height.y = 0.1f;
		cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
	}
}

//**********************************************************************************************************************
void Controller2DSystem::updateCameraFollowing()
{
	#if GARDEN_EDITOR
	auto editorSystem = EditorRenderSystem::Instance::tryGet();
	if (editorSystem && !editorSystem->isPlaying())
		return;
	#endif

	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	auto cameraTransformView = transformSystem->tryGetComponent(camera);
	auto cameraView = manager->tryGet<CameraComponent>(camera);
	auto characterEntities = LinkSystem::Instance::get()->findEntities(characterEntityTag);

	if (!cameraTransformView || !cameraTransformView->isActive() ||
		!cameraView || cameraView->type != ProjectionType::Orthographic)
	{
		return;
	}

	auto characterSystem = CharacterSystem::Instance::get();
	auto deltaTime = (float)InputSystem::Instance::get()->getDeltaTime();

	for (auto i = characterEntities.first; i != characterEntities.second; i++)
	{
		auto charTransformView = transformSystem->tryGetComponent(i->second);
		if (!charTransformView || !charTransformView->isActive())
			continue;

		auto characterView = characterSystem->tryGetComponent(i->second);
		if (!characterView)
			continue;

		auto cameraWidth = cameraView->p.orthographic.width;
		auto cameraHeight = cameraView->p.orthographic.height;
		auto posOffset = float2(cameraWidth.y - cameraWidth.x, 
			cameraHeight.y - cameraHeight.x) * followCenter;
		auto newPosition = lerpDelta((float2)cameraTransformView->position,
			(float2)characterView->getPosition() + posOffset, 1.0f - followFactor, deltaTime);
		cameraTransformView->position.x = newPosition.x;
		cameraTransformView->position.y = newPosition.y;
		break;
	}
}

//**********************************************************************************************************************
void Controller2DSystem::updateCharacterControll()
{
	#if GARDEN_EDITOR
	auto editorSystem = EditorRenderSystem::Instance::tryGet();
	if (editorSystem && !editorSystem->isPlaying())
		return;
	#endif

	auto characterEntities = LinkSystem::Instance::get()->findEntities(characterEntityTag);
	if (characterEntities.first == characterEntities.second)
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	auto characterSystem = CharacterSystem::Instance::get();
	auto deltaTime = (float)inputSystem->getDeltaTime();
	auto gravity = PhysicsSystem::Instance::get()->getGravity();

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
		auto characterView = characterSystem->tryGetComponent(i->second);
		if (!characterView || !characterView->getShape())
			continue;

		auto transformView = transformSystem->getComponent(i->second);
		if (transformView && !transformView->isActive())
			continue;

		auto position = characterView->getPosition();
		if (position.z != 0.0f)
			characterView->setPosition(float3(position.x, position.y, 0.0f));

		auto linearVelocity = characterView->getLinearVelocity();
		linearVelocity.x = lerpDelta(linearVelocity.x,
			horizontalVelocity, 1.0f - horizontalFactor, deltaTime);

		if (characterView->getGroundState() == CharacterGround::OnGround)
		{
			linearVelocity.y = isJumping ? jumpSpeed : 0.0f;
			canDoubleJump = true;
		}
		else
		{
			if (useDoubleJump && isJumping && canDoubleJump && !isLastJumping)
			{
				linearVelocity.y = jumpSpeed;
				canDoubleJump = false;
			}
			linearVelocity.y += gravity.y * deltaTime;
		}

		characterView->setLinearVelocity(linearVelocity);
		characterView->update(deltaTime, gravity);
	}

	isLastJumping = isJumping;
}
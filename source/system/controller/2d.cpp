// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/controller/2d.hpp"
#include "garden/system/ui/trigger.hpp"
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
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", Controller2DSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", Controller2DSystem::deinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", Controller2DSystem::update);
}
Controller2DSystem::~Controller2DSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", Controller2DSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", Controller2DSystem::deinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", Controller2DSystem::update);
	}

	unsetSingleton();
}

void Controller2DSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);

	camera = manager->createEntity();
	manager->reserveComponents(camera, 8);

	if (DoNotDestroySystem::Instance::has())
		manager->add<DoNotDestroyComponent>(camera);
	if (DoNotSerializeSystem::Instance::has())
		manager->add<DoNotSerializeComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->setPosition(f32x4(0.0f, 0.0f, -0.5f));
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformView->debugName = "Main Camera";
	#endif

	auto linkView = manager->add<LinkComponent>(camera);
	linkView->setTag("MainCamera");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getFramebufferSize();
	auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
	constexpr auto defaultSize = 2.0f;

	auto cameraView = manager->add<CameraComponent>(camera);
	cameraView->type = ProjectionType::Orthographic;
	cameraView->p.orthographic.depth = float2(0.0f, 1.0f);
	cameraView->p.orthographic.width = float2(-defaultSize, defaultSize) * aspectRatio;
	cameraView->p.orthographic.height = float2(-defaultSize, defaultSize);

	GARDEN_ASSERT_MSG(!graphicsSystem->camera, "Detected several main cameras");
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
		auto manager = Manager::Instance::get();
		GraphicsSystem::Instance::get()->camera = {};
		manager->destroy(camera);
		
		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", Controller2DSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void Controller2DSystem::update()
{
	SET_CPU_ZONE_SCOPED("2D Controller Update");

	if (useMouseControl)
		updateCameraControl();

	updateCharacterControl();
	updateCameraFollowing();
}

void Controller2DSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto cameraView = Manager::Instance::get()->tryGet<CameraComponent>(camera);
		if (cameraView)
		{
			auto framebufferSize = graphicsSystem->getFramebufferSize();
			auto aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
			cameraView->p.orthographic.width = cameraView->p.orthographic.height * aspectRatio;
		}
	}
}

//**********************************************************************************************************************
void Controller2DSystem::updateCameraControl()
{
	auto inputSystem = InputSystem::Instance::get();
	auto uiTranformSystem = UiTriggerSystem::Instance::tryGet();

	#if GARDEN_EDITOR
	auto wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
	#else
	auto wantCaptureMouse = false;
	#endif

	wantCaptureMouse |= inputSystem->getCursorMode() != CursorMode::Normal || 
		(uiTranformSystem && uiTranformSystem->getHovered());
	if (wantCaptureMouse)
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(camera);
	auto cameraView = manager->tryGet<CameraComponent>(camera);

	if (!transformView || !transformView->isActive() || 
		!cameraView || cameraView->type != ProjectionType::Orthographic)
	{
		return;
	}

	if (inputSystem->getMouseState(MouseButton::Right))
	{
		auto cursorDelta = inputSystem->getCursorDelta();
		auto windowSize = (float2)inputSystem->getWindowSize();
		auto orthoSize = float2(
			cameraView->p.orthographic.width.y - cameraView->p.orthographic.width.x,
			cameraView->p.orthographic.height.y - cameraView->p.orthographic.height.x);
		auto offset = cursorDelta / (windowSize / orthoSize);
		offset = (float2x2)transformView->calcModel() * offset;
		transformView->translate(-f32x4(offset.x, offset.y, 0.0f));
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
	auto cameraTransformView = manager->tryGet<TransformComponent>(camera);
	auto cameraView = manager->tryGet<CameraComponent>(camera);
	auto characterEntities = LinkSystem::Instance::get()->tryGet(characterEntityTag);

	if (!cameraTransformView || !cameraTransformView->isActive() ||
		!cameraView || cameraView->type != ProjectionType::Orthographic)
	{
		return;
	}

	auto deltaTime = (float)InputSystem::Instance::get()->getDeltaTime();
	for (auto i = characterEntities.first; i != characterEntities.second; i++)
	{
		auto charTransformView = manager->tryGet<TransformComponent>(i->second);
		if (!charTransformView || !charTransformView->isActive())
			continue;

		auto characterView = manager->tryGet<CharacterComponent>(i->second);
		if (!characterView)
			continue;

		auto cameraWidth = cameraView->p.orthographic.width;
		auto cameraHeight = cameraView->p.orthographic.height;
		auto posOffset = float2(cameraWidth.y - cameraWidth.x, 
			cameraHeight.y - cameraHeight.x) * followCenter;
		auto newPosition = lerpDelta((float2)cameraTransformView->getPosition(),
			(float2)characterView->getPosition() + posOffset, 1.0f - followLerpFactor, deltaTime);
		cameraTransformView->setPosition(f32x4(newPosition.x, newPosition.y, 
			cameraTransformView->getPosition().getZ()));
		break;
	}
}

//**********************************************************************************************************************
void Controller2DSystem::updateCharacterControl()
{
	#if GARDEN_EDITOR
	auto editorSystem = EditorRenderSystem::Instance::tryGet();
	if (editorSystem && !editorSystem->isPlaying())
		return;
	#endif

	auto characterEntities = LinkSystem::Instance::get()->tryGet(characterEntityTag);
	if (characterEntities.first == characterEntities.second)
		return;

	auto manager = Manager::Instance::get();
	auto inputSystem = InputSystem::Instance::get();
	auto deltaTime = (float)inputSystem->getDeltaTime();
	auto isJumping = inputSystem->getKeyboardState(KeyboardButton::Space);
	auto gravity = PhysicsSystem::Instance::get()->getGravity();

	auto horizontalVelocity = 0.0f;
	if (inputSystem->getKeyboardState(KeyboardButton::A) ||
		inputSystem->getKeyboardState(KeyboardButton::Left))
	{
		horizontalVelocity = -horizontalSpeed;
	}
	if (inputSystem->getKeyboardState(KeyboardButton::D) ||
		inputSystem->getKeyboardState(KeyboardButton::Right))
	{
		horizontalVelocity += horizontalSpeed;
	}

	for (auto i = characterEntities.first; i != characterEntities.second; i++)
	{
		auto characterView = manager->tryGet<CharacterComponent>(i->second);
		if (!characterView || !characterView->getShape())
			continue;

		auto transformView = manager->tryGet<TransformComponent>(i->second);
		if (transformView && !transformView->isActive())
			continue;

		auto position = characterView->getPosition();
		if (position.getZ() != 0.0f)
			characterView->setPosition(f32x4(position.getX(), position.getY(), 0.0f));

		auto linearVelocity = characterView->getLinearVelocity();
		linearVelocity.setX(lerpDelta(linearVelocity.getX(),
			horizontalVelocity, 1.0f - horizontalLerpFactor, deltaTime));

		if (characterView->getGroundState() == CharacterGround::OnGround)
		{
			linearVelocity.setY(isJumping ? jumpSpeed : 0.0f);
			canDoubleJump = true;
		}
		else
		{
			if (useDoubleJump && isJumping && canDoubleJump && !isLastJumping)
			{
				linearVelocity.setY(jumpSpeed);
				canDoubleJump = false;
			}
			linearVelocity.setY(linearVelocity.getY() + gravity.getY() * deltaTime);
		}

		characterView->setLinearVelocity(linearVelocity);
		characterView->update(deltaTime, gravity);
	}

	isLastJumping = isJumping;
}
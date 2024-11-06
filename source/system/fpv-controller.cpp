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

#include "garden/system/fpv-controller.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/character.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/link.hpp"
#include "garden/profiler.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#endif

using namespace garden;

//**********************************************************************************************************************
FpvControllerSystem::FpvControllerSystem(bool setSingleton) : Singleton(setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", FpvControllerSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", FpvControllerSystem::deinit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", FpvControllerSystem::update);
}
FpvControllerSystem::~FpvControllerSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", FpvControllerSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", FpvControllerSystem::deinit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", FpvControllerSystem::update);
	}

	unsetSingleton();
}

void FpvControllerSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", FpvControllerSystem::swapchainRecreate);

	auto manager = Manager::Instance::get();
	camera = manager->createEntity();
	manager->add<DoNotDestroyComponent>(camera);
	manager->add<DoNotSerializeComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->position = float3(0.0f, 2.0f, -2.0f);
	#if GARDEN_DEBUG | GARDEN_EDITOR
	transformView->debugName = "Main Camera";
	#endif

	auto linkView = manager->add<LinkComponent>(camera);
	linkView->setTag("MainCamera");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto framebufferSize = graphicsSystem->getFramebufferSize();

	auto cameraView = manager->add<CameraComponent>(camera);
	cameraView->type = ProjectionType::Perspective;
	cameraView->p.perspective.aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;

	GARDEN_ASSERT(!graphicsSystem->camera); // Several main cameras detected!
	graphicsSystem->camera = camera;
}
void FpvControllerSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsSystem::Instance::get()->camera = {};
		Manager::Instance::get()->destroy(camera);

		ECSM_UNSUBSCRIBE_FROM_EVENT("SwapchainRecreate", FpvControllerSystem::swapchainRecreate);
	}
}

//**********************************************************************************************************************
void FpvControllerSystem::update()
{
	SET_CPU_ZONE_SCOPED("FPV Controller Update");
	auto rotationQuat = updateCameraRotation();
	updateCameraControll(rotationQuat);
	updateCharacterControll();
}

void FpvControllerSystem::swapchainRecreate()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	const auto& swapchainChanges = graphicsSystem->getSwapchainChanges();

	if (swapchainChanges.framebufferSize)
	{
		auto cameraView = CameraSystem::Instance::get()->tryGetComponent(camera);
		if (cameraView)
		{
			auto framebufferSize = graphicsSystem->getFramebufferSize();
			cameraView->p.perspective.aspectRatio = (float)framebufferSize.x / (float)framebufferSize.y;
		}
	}
}

//**********************************************************************************************************************
quat FpvControllerSystem::updateCameraRotation()
{
	auto inputSystem = InputSystem::Instance::get();

	#if GARDEN_EDITOR
	if (ImGui::GetIO().WantCaptureMouse)
		return quat::identity;

	if (!inputSystem->getMouseState(MouseButton::Right))
	{
		if (inputSystem->getCursorMode() != CursorMode::Default &&
			inputSystem->isMouseReleased(MouseButton::Right))
		{
			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
			inputSystem->setCursorMode(CursorMode::Default);
		}
		return quat::identity;
	}

	if (inputSystem->getCursorMode() != CursorMode::Locked &&
		inputSystem->isMousePressed(MouseButton::Right))
	{
		ImGui::SetWindowFocus(nullptr);
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
		inputSystem->setCursorMode(CursorMode::Locked);
	}
	#endif

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(camera);
	if (!transformView || !transformView->isActive() )
		return quat::identity;

	auto cursorDelta = inputSystem->getCursorDelta();
	rotation += cursorDelta * mouseSensitivity * radians(0.1f);
	rotation.y = std::clamp(rotation.y, radians(-89.99f), radians(89.99f));
	auto rotationQuat = quat(rotation.y, float3::left) * quat(rotation.x, float3::bottom);
	transformView->rotation = rotationQuat;
	return rotationQuat;
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCameraControll(const quat& rotationQuat)
{
	auto inputSystem = InputSystem::Instance::get();

	#if GARDEN_EDITOR
	if (!inputSystem->getMouseState(MouseButton::Right))
		return;
	#endif

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(camera);
	if (!transformView || !transformView->isActive())
		return;

	auto boost = inputSystem->getKeyboardState(KeyboardButton::LeftShift) ? boostFactor : 1.0f;
	float3 flyDirection;

	if (inputSystem->getKeyboardState(KeyboardButton::A))
		flyDirection.x = -moveSpeed;
	else if (inputSystem->getKeyboardState(KeyboardButton::D))
		flyDirection.x = moveSpeed;
	if (inputSystem->getKeyboardState(KeyboardButton::Q) || inputSystem->getKeyboardState(KeyboardButton::LeftControl))
		flyDirection.y = -moveSpeed;
	else if (inputSystem->getKeyboardState(KeyboardButton::E) || inputSystem->getKeyboardState(KeyboardButton::Space))
		flyDirection.y = moveSpeed;
	if (inputSystem->getKeyboardState(KeyboardButton::S))
		flyDirection.z = -moveSpeed;
	else if (inputSystem->getKeyboardState(KeyboardButton::W))
		flyDirection.z = moveSpeed;
	flyDirection = (flyDirection * boost) * rotationQuat;

	auto deltaTime = (float)inputSystem->getDeltaTime();
	velocity = lerp(velocity, flyDirection, std::min(deltaTime * moveLerpFactor, 1.0f));
	transformView->position += velocity * deltaTime;
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCharacterControll()
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
			linearVelocity.y = isJumping ? jumpSpeed : 0.0f;
		else
			linearVelocity.y += gravity.y * deltaTime;

		characterView->setLinearVelocity(linearVelocity);
		characterView->update(deltaTime, gravity);
	}

	isLastJumping = isJumping;
}
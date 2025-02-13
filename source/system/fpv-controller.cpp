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
	updateMouseLock();
	auto rotationQuat = updateCameraRotation();
	updateCameraControl(rotationQuat);
	updateCharacterControl();
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

void FpvControllerSystem::updateMouseLock()
{
	auto inputSystem = InputSystem::Instance::get();
	if (inputSystem->isKeyboardPressed(KeyboardButton::Tab)) // TODO: && get the exact button from the input system
		isMouseLocked = !isMouseLocked;

	if (isMouseLocked)
	{
		if (inputSystem->getCursorMode() != CursorMode::Locked)
		{
			#if GARDEN_EDITOR
			ImGui::SetWindowFocus(nullptr);
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
			#endif
			inputSystem->setCursorMode(CursorMode::Locked);
		}
	}
	else
	{
		if (inputSystem->getCursorMode() != CursorMode::Normal)
		{
			#if GARDEN_EDITOR
			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
			#endif
			inputSystem->setCursorMode(CursorMode::Normal);
		}
	}
}

//**********************************************************************************************************************
quat FpvControllerSystem::updateCameraRotation()
{
	auto transformView = TransformSystem::Instance::get()->tryGetComponent(camera);
	if (!isMouseLocked || !transformView || !transformView->isActive() )
		return quat::identity;

	auto cursorDelta = InputSystem::Instance::get()->getCursorDelta();
	rotation += cursorDelta * mouseSensitivity * radians(0.1f);
	rotation.y = std::clamp(rotation.y, radians(-89.99f), radians(89.99f));
	auto rotationQuat = quat(rotation.y, float3::left) * quat(rotation.x, float3::bottom);
	transformView->rotation = rotationQuat;
	return rotationQuat;
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCameraControl(const quat& rotationQuat)
{
	#if GARDEN_EDITOR
	auto editorSystem = EditorRenderSystem::Instance::tryGet();
	if (editorSystem && editorSystem->isPlaying())
		return;
	#endif

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(camera);
	if (!transformView || !transformView->isActive())
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto deltaTime = (float)inputSystem->getDeltaTime();
	auto flyVector = float3(0.0f);

	if (isMouseLocked)
	{
		float boost = 1.0f;
		if (inputSystem->getKeyboardState(KeyboardButton::LeftShift))
		{
			boost = boostFactor * boostAccum;
			boostAccum += deltaTime * boostFactor;
		}
		else
		{
			boostAccum = 1.0f;
		}

		if (inputSystem->getKeyboardState(KeyboardButton::A))
			flyVector.x = -moveSpeed;
		else if (inputSystem->getKeyboardState(KeyboardButton::D))
			flyVector.x = moveSpeed;
		if (inputSystem->getKeyboardState(KeyboardButton::Q) || inputSystem->getKeyboardState(KeyboardButton::LeftControl))
			flyVector.y = -moveSpeed;
		else if (inputSystem->getKeyboardState(KeyboardButton::E) || inputSystem->getKeyboardState(KeyboardButton::Space))
			flyVector.y = moveSpeed;
		if (inputSystem->getKeyboardState(KeyboardButton::S))
			flyVector.z = -moveSpeed;
		else if (inputSystem->getKeyboardState(KeyboardButton::W))
			flyVector.z = moveSpeed;
		flyVector = (flyVector * boost) * rotationQuat;
	}

	velocity = lerpDelta(velocity, flyVector, 1.0f - moveLerpFactor, deltaTime);
	transformView->position += velocity * deltaTime;
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCharacterControl()
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
	auto isJumping = inputSystem->getKeyboardState(KeyboardButton::Space);
	const auto& gravity = PhysicsSystem::Instance::get()->getGravity();
	const auto& cameraConstants = GraphicsSystem::Instance::get()->getCurrentCameraConstants();

	auto velocity = float3(0.0f);
	if (inputSystem->getKeyboardState(KeyboardButton::W))
		velocity = (float3)cameraConstants.viewDir;
	if (inputSystem->getKeyboardState(KeyboardButton::S))
		velocity -= (float3)cameraConstants.viewDir;
	if (inputSystem->getKeyboardState(KeyboardButton::D))
		velocity += (float3)(cameraConstants.inverseView * float4(float3::right, 1.0f));
	if (inputSystem->getKeyboardState(KeyboardButton::A))
		velocity -= (float3)(cameraConstants.inverseView * float4(float3::right, 1.0f));
	
	if (velocity != float3(0.0f))
	{
		velocity.y = 0.0f;
		velocity = normalize(velocity);
		velocity *= moveSpeed;
	}

	if (inputSystem->getKeyboardState(KeyboardButton::LeftShift))
		velocity *= boostFactor;

	for (auto i = characterEntities.first; i != characterEntities.second; i++)
	{
		auto characterView = characterSystem->tryGetComponent(i->second);
		if (!characterView || !characterView->getShape())
			continue;

		auto transformView = transformSystem->tryGetComponent(i->second);
		if (transformView && !transformView->isActive())
			continue;

		auto linearVelocity = characterView->getLinearVelocity();
		linearVelocity.x = lerpDelta(linearVelocity.x, velocity.x, 1.0f - moveLerpFactor, deltaTime);
		linearVelocity.z = lerpDelta(linearVelocity.z, velocity.z, 1.0f - moveLerpFactor, deltaTime);

		if (characterView->getGroundState() == CharacterGround::OnGround)
			linearVelocity.y = isJumping ? jumpSpeed : 0.0f;
		else
			linearVelocity.y += gravity.y * deltaTime;

		characterView->setLinearVelocity(linearVelocity);
		characterView->update(deltaTime, gravity);
	}
}
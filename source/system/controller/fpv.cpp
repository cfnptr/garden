// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/controller/fpv.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/transform.hpp"
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
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", FpvControllerSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Update", FpvControllerSystem::update);
}
void FpvControllerSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("SwapchainRecreate", FpvControllerSystem::swapchainRecreate);

	camera = manager->createEntity();
	manager->reserveComponents(camera, 8);

	if (DoNotDestroySystem::Instance::has())
		manager->add<DoNotDestroyComponent>(camera);
	if (DoNotSerializeSystem::Instance::has())
		manager->add<DoNotSerializeComponent>(camera);
	if (PbrLightingSystem::Instance::has())
		manager->add<PbrLightingComponent>(camera);

	auto transformView = manager->add<TransformComponent>(camera);
	transformView->setPosition(f32x4(0.0f, 2.0f, -2.0f));
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformView->debugName = "Main Camera";
	#endif

	auto linkView = manager->add<LinkComponent>(camera);
	linkView->setTag("MainCamera");

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto frameSize = graphicsSystem->getFramebufferSize();

	auto cameraView = manager->add<CameraComponent>(camera);
	cameraView->type = ProjectionType::Perspective;
	cameraView->p.perspective.aspectRatio = (float)frameSize.x / (float)frameSize.y;

	GARDEN_ASSERT_MSG(!graphicsSystem->camera, "Detected several main cameras");
	graphicsSystem->camera = camera;
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
		auto cameraView = Manager::Instance::get()->tryGet<CameraComponent>(camera);
		if (cameraView)
		{
			auto frameSize = graphicsSystem->getFramebufferSize();
			cameraView->p.perspective.aspectRatio = (float)frameSize.x / (float)frameSize.y;
		}
	}
}

void FpvControllerSystem::updateMouseLock()
{
	auto inputSystem = InputSystem::Instance::get();

	#if GARDEN_EDITOR
	auto wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
	#else
	auto wantCaptureMouse = false;
	#endif

	// TODO: && get the exact button from the input system
	if (!wantCaptureMouse && inputSystem->isKeyPressed(KeyboardButton::Tab)) 
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
	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(camera);
	if (!isMouseLocked || !transformView || !transformView->isActive() )
		return quat::identity;

	auto cursorDelta = InputSystem::Instance::get()->getCursorDelta();
	rotation += cursorDelta * mouseSensitivity * radians(0.1f);
	rotation.y = std::clamp(rotation.y, radians(-89.99f), radians(89.99f));
	auto cameraRotation = quat(rotation.y, f32x4::right) * quat(rotation.x, f32x4::bottom);
	transformView->setRotation(cameraRotation);
	return cameraRotation;
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCameraControl(quat cameraRotation)
{
	#if GARDEN_EDITOR
	auto editorSystem = EditorRenderSystem::Instance::tryGet();
	if (editorSystem && editorSystem->isPlaying())
		return;
	#endif

	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(camera);
	if (!transformView || !transformView->isActive())
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto deltaTime = (float)inputSystem->getDeltaTime();
	auto flyVector = f32x4::zero;

	if (isMouseLocked)
	{
		float boost = 1.0f;
		if (inputSystem->getKeyState(KeyboardButton::LeftShift))
		{
			boost = boostFactor * boostAccum;
			boostAccum += deltaTime * boostFactor;
		}
		else
		{
			boostAccum = 1.0f;
		}

		if (inputSystem->getKeyState(KeyboardButton::A))
			flyVector.setX(-moveSpeed);
		else if (inputSystem->getKeyState(KeyboardButton::D))
			flyVector.setX(moveSpeed);
		if (inputSystem->getKeyState(KeyboardButton::Q) || inputSystem->getKeyState(KeyboardButton::LeftControl))
			flyVector.setY(-moveSpeed);
		else if (inputSystem->getKeyState(KeyboardButton::E) || inputSystem->getKeyState(KeyboardButton::Space))
			flyVector.setY(moveSpeed);
		if (inputSystem->getKeyState(KeyboardButton::S))
			flyVector.setZ(-moveSpeed);
		else if (inputSystem->getKeyState(KeyboardButton::W))
			flyVector.setZ(moveSpeed);
		flyVector = (flyVector * boost) * cameraRotation;
	}

	velocity = lerpDelta(velocity, flyVector, 1.0f - moveLerpFactor, deltaTime);
	transformView->translate(velocity * deltaTime);
}

//**********************************************************************************************************************
void FpvControllerSystem::updateCharacterControl()
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
	auto isJumping = inputSystem->getKeyState(KeyboardButton::Space);
	const auto& gravity = PhysicsSystem::Instance::get()->getGravity();
	const auto& cc = GraphicsSystem::Instance::get()->getCommonConstants();

	auto velocity = f32x4::zero;
	if (inputSystem->getKeyState(KeyboardButton::W))
		velocity = (f32x4)cc.viewDir;
	if (inputSystem->getKeyState(KeyboardButton::S))
		velocity -= (f32x4)cc.viewDir;
	if (velocity != f32x4::zero)
	{
		velocity.setY(0.0f);
		velocity = normalize3(velocity);
	}

	if (inputSystem->getKeyState(KeyboardButton::D))
		velocity += cc.inverseView * f32x4(f32x4::right, 1.0f);
	if (inputSystem->getKeyState(KeyboardButton::A))
		velocity -= cc.inverseView * f32x4(f32x4::right, 1.0f);
	if (velocity != f32x4::zero)
	{
		velocity.setY(0.0f);
		velocity = normalize3(velocity);
		velocity *= moveSpeed;
	}

	if (inputSystem->getKeyState(KeyboardButton::LeftShift))
		velocity *= boostFactor;

	for (auto i = characterEntities.first; i != characterEntities.second; i++)
	{
		auto characterView = manager->tryGet<CharacterComponent>(i->second);
		if (!characterView || !characterView->getShape())
			continue;

		auto transformView = manager->tryGet<TransformComponent>(i->second);
		if (transformView && !transformView->isActive())
			continue;

		auto groundState = characterView->getGroundState();
		if (groundState == CharacterGround::OnGround)
			velocity += characterView->getGroundVelocity();

		auto linearVelocity = characterView->getLinearVelocity();
		linearVelocity.setX(lerpDelta(linearVelocity.getX(), velocity.getX(), 1.0f - moveLerpFactor, deltaTime));
		linearVelocity.setZ(lerpDelta(linearVelocity.getZ(), velocity.getZ(), 1.0f - moveLerpFactor, deltaTime));

		if (canSwim)
		{
			if (isJumping) linearVelocity.setY(swimSpeed * boostFactor);
			else linearVelocity += gravity * deltaTime;
			linearVelocity *= swimResist;
		}
		else
		{
			if (groundState == CharacterGround::OnGround)
				linearVelocity.setY(isJumping ? jumpSpeed : 0.0f);
			else linearVelocity += gravity * deltaTime;
		}

		characterView->setLinearVelocity(linearVelocity);
		characterView->update(deltaTime, gravity);
	}
}
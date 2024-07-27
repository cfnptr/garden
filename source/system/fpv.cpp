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

/*
#include "garden/system/fpv.hpp"
#include "garden/system/render/editor.hpp"

using namespace garden;

#define PLAYER_RADIUS 0.25f
#define PLAYER_HEIGHT 1.75f

#define PLAYER_PX_HEIGHT (PLAYER_HEIGHT - (PLAYER_RADIUS * 2.0f))
#define PLAYER_PX_HALF_HEIGHT (PLAYER_PX_HEIGHT * 0.5f)

//**********************************************************************************************************************
void FpvSystem::initialize()
{
	lastJumpTime = graphicsSystem->getTime();
}
void FpvSystem::update()
{
	if (!graphicsSystem->camera)
		return;

	if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::Tab))
	{
		if (!lastTabState)
		{
			if (graphicsSystem->getCursorMode() == CursorMode::Default)
			{
				graphicsSystem->setCursorMode(CursorMode::Locked);
				lastCursorPosition = graphicsSystem->getCursorPosition();
				
				#if GARDEN_EDITOR
				ImGui::SetWindowFocus(nullptr);
				ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
				#endif
			}
			else
			{
				graphicsSystem->setCursorMode(CursorMode::Default);

				#if GARDEN_EDITOR
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
				#endif
			}
			lastTabState = true;
		}
	}
	else
	{
		lastTabState = false;
	}

	auto manager = Manager::getInstance();
	auto camera = graphicsSystem->camera;
	auto transformView = TransformSystem::getInstance()->get(camera);
	// auto hasRigidBody = manager->has<RigidBodyComponent>(camera);

	if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::F10) &&
		manager->has<PhysicsSystem>())
	{
		if (!lastF10State)
		{
			auto physicsSystem = getPhysicsSystem();
			if (!material)
				material = physicsSystem->createMaterial(1.0f, 1.0f, 0.0f);

			if (!hasRigidBody)
			{
				auto capsuleShape = physicsSystem->createCapsuleShape(
					material, PLAYER_RADIUS, PLAYER_PX_HALF_HEIGHT);
				auto shapeView = physicsSystem->get(capsuleShape);
				shapeView->setPose(float3(0.0f, -(PLAYER_PX_HALF_HEIGHT +
					PLAYER_RADIUS * 0.5f), 0.0f),
					quat((float)M_PI_2, float3(0.0f, 0.0f, 1.0f)));
				shapeView->setContactOffset(0.01f);
				shapeView->setRestOffset(0.001f);

				auto triggerShape = physicsSystem->createSphereShape(
					material, PLAYER_RADIUS * 0.9f);
				shapeView = physicsSystem->get(triggerShape);
				shapeView->setPose(float3(0.0f, -(PLAYER_PX_HALF_HEIGHT +
					PLAYER_RADIUS * 0.5f) * 2.0f, 0.0f));
				shapeView->setContactOffset(0.01f);
				shapeView->setRestOffset(0.001f);
				shapeView->setTrigger(true);

				auto rigidBodyView = manager->add<RigidBodyComponent>(camera);
				rigidBodyView->setStatic(false);
				rigidBodyView->setAngularLockX(true);
				rigidBodyView->setAngularLockY(true);
				rigidBodyView->setAngularLockZ(true);
				rigidBodyView->attachShape(capsuleShape);
				rigidBodyView->attachShape(triggerShape);
				rigidBodyView->setPose(transformView->position);
				rigidBodyView->setLinearDamping(0.025f);
				rigidBodyView->setSleepThreshold(0.1f);
				rigidBodyView->setSolverIterCount(16, 4);
				rigidBodyView->calcMassAndInertia(220.0f);
				rigidBodyView->updatePose = false;
				// TODO: set max terminal human velocity.
				hasRigidBody = true;
			}
			else
			{
				auto rigidBodyComponent = manager->get<RigidBodyComponent>(camera);
				auto shapeCount = rigidBodyComponent->getShapeCount();
				vector<ID<Shape>> shapes(shapeCount);
				rigidBodyComponent->getShapes(shapes);
				for (uint32 i = 0; i < shapeCount; i++)
					physicsSystem->destroy(shapes[i]);
				manager->remove<RigidBodyComponent>(camera);
				hasRigidBody = false;
			}
			lastF10State = true;
		}
	}
	else
	{
		lastF10State = false;
	}


	if (graphicsSystem->getCursorMode() == CursorMode::Default)
		return;
	
	auto deltaTime = (float)graphicsSystem->getDeltaTime();
	auto cursorPosition = graphicsSystem->getCursorPosition();
	auto deltaCursorPosition = lastCursorPosition - cursorPosition;
	lastCursorPosition = cursorPosition;
	rotation -= deltaCursorPosition * viewSensitivity * radians(0.1f);
	rotation.y = std::clamp(rotation.y, radians(-89.99f), radians(89.99f));

	auto rotationQuat = quat(rotation.y, float3::left) *
		quat(rotation.x, float3::bottom);
	transformView->rotation = rotationQuat;

	auto boost = graphicsSystem->isKeyboardButtonPressed(
		KeyboardButton::LeftShift) ? moveBoost : 1.0f;
	float3 moveDirection;

	if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::A))
		moveDirection.x = -moveSensitivity;
	else if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::D))
		moveDirection.x = moveSensitivity;
	if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::Q) ||
		graphicsSystem->isKeyboardButtonPressed(KeyboardButton::LeftControl)) // && !hasRigidBody)
		moveDirection.y = -moveSensitivity;
	else if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::E) ||
		graphicsSystem->isKeyboardButtonPressed(KeyboardButton::Space)) // && !hasRigidBody)
		moveDirection.y = moveSensitivity;
	if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::S))
		moveDirection.z = -moveSensitivity;
	else if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::W))
		moveDirection.z = moveSensitivity;
	moveDirection = moveDirection * boost * rotationQuat;

	if (true) // !hasRigidBody)
	{
		velocity = lerp(velocity, moveDirection,
			std::min(deltaTime * lerpMultiplier, 1.0f));
		transformView->position += velocity * deltaTime;
	}
	else
	{
		auto rigidBodyComponent = manager->get<RigidBodyComponent>(camera);
		if (graphicsSystem->isKeyboardButtonPressed(KeyboardButton::Space) &&
			triggerCount > 0 && lastJumpTime < graphicsSystem->getTime())
		{
			rigidBodyComponent->addForce(float3(0.0f, 250.0f, 0.0f),
				RigidBodyComponent::ForceType::Impulse);
			lastJumpTime = graphicsSystem->getTime() + jumpDelay;
		}
		
		moveDirection.y = velocity.y = 0.0f;
		if (length2(moveDirection) > 0.0f)
			moveDirection = normalize(moveDirection);
		auto delta = std::min(deltaTime * lerpMultiplier, 1.0f);
		velocity.x = lerp(velocity.x, moveDirection.x, delta);
		velocity.z = lerp(velocity.z, moveDirection.z, delta);
		rigidBodyComponent->addForce(velocity * 4000.0f * std::max(boost * 0.2f, 1.0f));

		auto velocity = rigidBodyComponent->getLinearVelocity();
		velocity.x = lerp(velocity.x, 0.0f, delta);
		velocity.z = lerp(velocity.z, 0.0f, delta);
		rigidBodyComponent->setLinearVelocity(velocity);
	}
}

//--------------------------------------------------------------------------------------------------
void FpvSystem::onTrigger(const TriggerData& data)
{
	if (graphicsSystem->camera != data.triggerEntity ||
		!Manager::getInstance()->has<RigidBodyComponent>(graphicsSystem->camera))
	{
		return;
	}

	if (data.isEntered)
		triggerCount++;
	else
		triggerCount--;
}
void FpvSystem::postSimulate()
{
	if (!graphicsSystem->camera)
		return;

	auto manager = Manager::getInstance();
	auto camera = graphicsSystem->camera; quat rotation;
	auto rigidBodyComponent = manager->tryGet<RigidBodyComponent>(camera);
	if (!rigidBodyComponent)
		return;

	auto transformView = TransformSystem::getInstance()->get(camera);
	rigidBodyComponent->getPose(transformView->position, rotation);
}

*/
// TODO: fix strange shuttering when rotating camera on macOS, problem inside glfw
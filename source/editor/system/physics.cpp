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

#include "garden/editor/system/physics.hpp"

#if GARDEN_EDITOR
#include "garden/system/physics.hpp"
#include "math/angles.hpp"

using namespace garden;

//**********************************************************************************************************************
PhysicsEditorSystem::PhysicsEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", PhysicsEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", PhysicsEditorSystem::deinit);
	
}
PhysicsEditorSystem::~PhysicsEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", PhysicsEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", PhysicsEditorSystem::deinit);
	}
}

void PhysicsEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());
	SUBSCRIBE_TO_EVENT("EditorRender", PhysicsEditorSystem::editorRender);

	EditorRenderSystem::getInstance()->registerEntityInspector<RigidbodyComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	},
	inspectorPriority);
}
void PhysicsEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<RigidbodyComponent>();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("EditorRender", PhysicsEditorSystem::editorRender);
}

//**********************************************************************************************************************
void PhysicsEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto editorSystem = EditorRenderSystem::getInstance();
	if (!isEnabled || !editorSystem->selectedEntity || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto physicsSystem = PhysicsSystem::getInstance();
	auto rigidbodyView = physicsSystem->tryGet(editorSystem->selectedEntity);

	if (rigidbodyView && rigidbodyView->getShape())
	{
		auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
		auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		auto model = calcModel(position - (float3)cameraConstants.cameraPos, rotation);
		auto shapeView = physicsSystem->get(rigidbodyView->getShape());
		auto subType = shapeView->getSubType();

		graphicsSystem->startRecording(CommandBufferType::Frame);
		{
			SET_GPU_DEBUG_LABEL("Rigidbody Box AABB", Color::transparent);
			framebufferView->beginRenderPass(float4(0.0f));

			if (subType == ShapeSubType::Box)
			{
				auto mvp = cameraConstants.viewProj * model * scale(shapeView->getBoxHalfExtent() * 2.0f);
				graphicsSystem->drawAabb(mvp, (float4)aabbColor);
			}
			
			framebufferView->endRenderPass();
		}
		graphicsSystem->stopRecording();
	}
}

//**********************************************************************************************************************
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView)
{
	auto physicsSystem = PhysicsSystem::getInstance();
	auto shape = rigidbodyView->getShape();
	auto shapeView = physicsSystem->get(shape);
	auto isChanged = false;

	auto halfExtent = shapeView->getBoxHalfExtent();
	isChanged |= ImGui::DragFloat3("Half Extent", &halfExtent, 0.01f);
	if (ImGui::BeginPopupContextItem("halfExtent"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			halfExtent = float3(0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	auto convexRadius = shapeView->getBoxConvexRadius();
	isChanged |= ImGui::DragFloat("Convex Radius", &convexRadius, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			convexRadius = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged && halfExtent >= convexRadius && convexRadius >= 0.0f)
	{
		physicsSystem->destroyShared(shape);
		shape = physicsSystem->createSharedBoxShape(halfExtent, convexRadius);
		rigidbodyView->setShape(shape);
	}
}
//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView)
{
	auto physicsSystem = PhysicsSystem::getInstance();
	auto shape = rigidbodyView->getShape();

	ImGui::BeginDisabled(shape && !rigidbodyView->canBeKinematicOrDynamic());
	const auto mTypes = "Static\0Kinematic\0Dynamic\00";
	auto motionType = rigidbodyView->getMotionType();
	if (ImGui::Combo("Motion Type", &motionType, mTypes))
		rigidbodyView->setMotionType(motionType);
	ImGui::EndDisabled();

	int shapeType = 0;
	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);
		switch (shapeView->getSubType())
		{
		case ShapeSubType::Box:
			shapeType = 2;
			break;
		default:
			shapeType = 1;
			break;
		}
	}

	const auto sTypes = "None\0Custom\0Box\00";
	if (ImGui::Combo("Shape", &shapeType, sTypes))
	{
		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			rigidbodyView->setShape({});
			break;
		case 2:
			physicsSystem->destroyShared(shape);
			shape = physicsSystem->createSharedBoxShape(float3(0.5f));
			rigidbodyView->setShape(shape);
			break;
		default:
			break;
		}
	}

	if (shapeType == 2)
		renderBoxShape(rigidbodyView);
}

//**********************************************************************************************************************
void PhysicsEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto physicsSystem = PhysicsSystem::getInstance();
	if (ImGui::BeginItemTooltip())
	{
		auto rigidbodyView = physicsSystem->get(entity);
		ImGui::Text("Active: %s", rigidbodyView->isActive() ? "true" : "false"); // TODO: show more info
		ImGui::EndTooltip();
	}

	if (isOpened)
	{
		auto rigidbodyView = physicsSystem->get(entity);

		auto isActive = rigidbodyView->isActive();
		if (ImGui::Checkbox("Active", &isActive))
		{
			if (isActive && rigidbodyView->getShape())
				rigidbodyView->activate();
			else
				rigidbodyView->deactivate();
		}

		ImGui::BeginDisabled(!rigidbodyView->getShape());
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		if (ImGui::DragFloat3("Position", &position, 0.01f))
			rigidbodyView->setPosition(position);
		if (ImGui::BeginPopupContextItem("position"))
		{
			if (ImGui::MenuItem("Reset Default"))
				rigidbodyView->setPosition(float3(0.0f));
			ImGui::EndPopup();
		}

		if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f))
		{
			auto difference = newEulerAngles - oldEulerAngles;
			rotation *= quat(radians(difference));
			rigidbodyView->setRotation(rotation);
			oldEulerAngles = newEulerAngles;
		}
		if (ImGui::BeginPopupContextItem("rotation"))
		{
			if (ImGui::MenuItem("Reset Default"))
				rigidbodyView->setRotation(quat::identity);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			auto rotation = radians(newEulerAngles);
			ImGui::Text("Rotation in degrees\nRadians: %.3f, %.3f, %.3f",
				rotation.x, rotation.y, rotation.z);
			ImGui::EndTooltip();
		}
		ImGui::EndDisabled();

		renderShapeProperties(rigidbodyView);
	}

	auto editorSystem = EditorRenderSystem::getInstance();
	if (editorSystem->selectedEntity != selectedEntity)
	{
		if (editorSystem->selectedEntity)
		{
			auto rigidbodyView = physicsSystem->get(editorSystem->selectedEntity);
			if (rigidbodyView->getShape())
			{
				auto rotation = rigidbodyView->getRotation();
				oldEulerAngles = newEulerAngles = degrees(rotation.toEulerAngles());
				oldRotation = rotation;
			}
		}
		selectedEntity = editorSystem->selectedEntity;
	}
	else
	{
		if (editorSystem->selectedEntity)
		{
			auto rigidbodyView = physicsSystem->get(editorSystem->selectedEntity);
			if (rigidbodyView->getShape())
			{
				auto rotation = rigidbodyView->getRotation();
				if (oldRotation != rotation)
				{
					oldEulerAngles = newEulerAngles = degrees(rotation.toEulerAngles());
					oldRotation = rotation;
				}
			}
		}
	}
}
#endif
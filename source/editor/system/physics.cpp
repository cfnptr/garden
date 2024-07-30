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
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView, AllowedDOF allowedDofCache)
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
		auto isSensor = rigidbodyView->isSensor();
		rigidbodyView->setShape({});
		physicsSystem->destroyShared(shape);
		shape = physicsSystem->createSharedBoxShape(halfExtent, convexRadius);
		rigidbodyView->setShape(shape, false, false, isSensor, allowedDofCache);
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView, AllowedDOF& allowedDofCached)
{
	auto physicsSystem = PhysicsSystem::getInstance();
	auto shape = rigidbodyView->getShape();
	auto isSensor = rigidbodyView->isSensor();

	ImGui::BeginDisabled(shape && !rigidbodyView->canBeKinematicOrDynamic());
	const auto mTypes = "Static\0Kinematic\0Dynamic\00";
	auto motionType = rigidbodyView->getMotionType();
	if (ImGui::Combo("Motion Type", &motionType, mTypes))
	{
		if (motionType != MotionType::Static)
			allowedDofCached = AllowedDOF::All;
		rigidbodyView->setMotionType(motionType);
	}
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
			rigidbodyView->setShape({});
			physicsSystem->destroyShared(shape);
			
			break;
		case 2:
			rigidbodyView->setShape({});
			physicsSystem->destroyShared(shape);
			shape = physicsSystem->createSharedBoxShape(float3(0.5f));
			rigidbodyView->setShape(shape, false, false, isSensor, allowedDofCached);
			break;
		default:
			break;
		}
	}

	if (shapeType == 2)
		renderBoxShape(rigidbodyView, allowedDofCached);
}

//**********************************************************************************************************************
static void renderAdvancedProperties(View<RigidbodyComponent> rigidbodyView, AllowedDOF& allowedDofCached)
{
	ImGui::Indent();
	ImGui::SeparatorText("Degrees of Freedom (DOF)");

	auto shape = rigidbodyView->getShape();
	if (shape)
		allowedDofCached = rigidbodyView->getAllowedDOF();
	auto isAnyChanged = false;

	ImGui::BeginDisabled(rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::Text("Translation:"); ImGui::SameLine();
	ImGui::PushID("dofTranslation");
	auto transX = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationX);
	isAnyChanged |= ImGui::Checkbox("X", &transX); ImGui::SameLine();
	auto transY = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationY);
	isAnyChanged |= ImGui::Checkbox("Y", &transY); ImGui::SameLine();
	auto transZ = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &transZ);
	ImGui::PopID();

	ImGui::Text("Rotation:"); ImGui::SameLine();
	ImGui::PushID("dofRotation");
	auto rotX = hasAnyFlag(allowedDofCached, AllowedDOF::RotationX);
	isAnyChanged |= ImGui::Checkbox("X", &rotX); ImGui::SameLine();
	auto rotY = hasAnyFlag(allowedDofCached, AllowedDOF::RotationY);
	isAnyChanged |= ImGui::Checkbox("Y", &rotY); ImGui::SameLine();
	auto rotZ = hasAnyFlag(allowedDofCached, AllowedDOF::RotationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &rotZ);
	ImGui::PopID();
	ImGui::EndDisabled();

	if (isAnyChanged)
	{
		allowedDofCached = AllowedDOF::None;
		if (transX) allowedDofCached |= AllowedDOF::TranslationX;
		if (transY) allowedDofCached |= AllowedDOF::TranslationY;
		if (transZ) allowedDofCached |= AllowedDOF::TranslationZ;
		if (rotX) allowedDofCached |= AllowedDOF::RotationX;
		if (rotY) allowedDofCached |= AllowedDOF::RotationY;
		if (rotZ) allowedDofCached |= AllowedDOF::RotationZ;

		if (shape)
		{
			auto isSensor = rigidbodyView->isSensor();
			rigidbodyView->setShape({});
			rigidbodyView->setShape(shape, false, false, isSensor, allowedDofCached);
		}
	}

	ImGui::Unindent();
}

//**********************************************************************************************************************
void PhysicsEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	auto physicsSystem = PhysicsSystem::getInstance();
	if (ImGui::BeginItemTooltip())
	{
		auto rigidbodyView = physicsSystem->get(entity);
		ImGui::Text("Has shape: %s, Active: %s", rigidbodyView->getShape() ? "true" : "false",
			rigidbodyView->isActive() ? "true" : "false");
		auto motionType = rigidbodyView->getMotionType();
		if (motionType == MotionType::Static)
			ImGui::Text("Motion type: Static");
		else if (motionType == MotionType::Kinematic)
			ImGui::Text("Motion type: Kinematic");
		else if (motionType == MotionType::Dynamic)
			ImGui::Text("Motion type: Dynamic");
		else ImGui::Text("Motion type: <unknown>");
		ImGui::EndTooltip();
	}

	if (isOpened)
	{
		auto rigidbodyView = physicsSystem->get(entity);
		auto shape = rigidbodyView->getShape();

		ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() == MotionType::Static);
		auto isActive = rigidbodyView->isActive();
		if (ImGui::Checkbox("Active", &isActive))
		{
			if (isActive)
				rigidbodyView->activate();
			else
				rigidbodyView->deactivate();
		}

		ImGui::SameLine();
		auto isSensor = rigidbodyView->isSensor();
		if (ImGui::Checkbox("Sensor", &isSensor))
			rigidbodyView->setSensor(isSensor);
		ImGui::EndDisabled();
		
		ImGui::BeginDisabled(!shape);
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		if (ImGui::DragFloat3("Position", &position, 0.01f))
			rigidbodyView->setPosition(position, false);
		if (ImGui::BeginPopupContextItem("position"))
		{
			if (ImGui::MenuItem("Reset Default"))
				rigidbodyView->setPosition(float3(0.0f), false);
			ImGui::EndPopup();
		}

		if (ImGui::DragFloat3("Rotation", &newEulerAngles, 0.3f))
		{
			auto difference = newEulerAngles - oldEulerAngles;
			rotation *= quat(radians(difference));
			rigidbodyView->setRotation(rotation, false);
			oldEulerAngles = newEulerAngles;
		}
		if (ImGui::BeginPopupContextItem("rotation"))
		{
			if (ImGui::MenuItem("Reset Default"))
				rigidbodyView->setRotation(quat::identity, false);
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

		renderShapeProperties(rigidbodyView, allowedDofCached);

		if (ImGui::CollapsingHeader("Advanced Properties"))
			renderAdvancedProperties(rigidbodyView, allowedDofCached);
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
		allowedDofCached = AllowedDOF::All;
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
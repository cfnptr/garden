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
	SUBSCRIBE_TO_EVENT("Init", PhysicsEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", PhysicsEditorSystem::deinit);
	
}
PhysicsEditorSystem::~PhysicsEditorSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", PhysicsEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", PhysicsEditorSystem::deinit);
	}
}

void PhysicsEditorSystem::init()
{
	SUBSCRIBE_TO_EVENT("EditorRender", PhysicsEditorSystem::editorRender);

	EditorRenderSystem::get()->registerEntityInspector<RigidbodyComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onRigidbodyInspector(entity, isOpened);
	},
	rigidbodyInspectorPriority);

	if (Manager::get()->has<CharacterSystem>())
	{
		EditorRenderSystem::get()->registerEntityInspector<CharacterComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCharacterInspector(entity, isOpened);
		},
		characterInspectorPriority);
	}
}
void PhysicsEditorSystem::deinit()
{
	auto editorSystem = EditorRenderSystem::get();
	editorSystem->unregisterEntityInspector<RigidbodyComponent>();
	editorSystem->tryUnregisterEntityInspector<CharacterComponent>();

	if (Manager::get()->isRunning())
		UNSUBSCRIBE_FROM_EVENT("EditorRender", PhysicsEditorSystem::editorRender);
}

//**********************************************************************************************************************
static void renderShapeAABB(float3 position, quat rotation, ID<Shape> shape, Color aabbColor)
{
	auto physicsSystem = PhysicsSystem::get();
	auto graphicsSystem = GraphicsSystem::get();

	auto shapeView = physicsSystem->get(shape);
	if (shapeView->getType() == ShapeType::Decorated)
	{
		float3 innerPos; quat innerRot;
		shapeView->getPosAndRot(innerPos, innerRot);
		position += rotation * innerPos; rotation *= innerRot;
		auto innerShape = shapeView->getInnerShape();
		shapeView = physicsSystem->get(innerShape);
	}

	auto& cameraConstants = graphicsSystem->getCurrentCameraConstants();
	auto model = calcModel(position - (float3)cameraConstants.cameraPos, rotation);
	auto framebufferView = graphicsSystem->get(graphicsSystem->getSwapchainFramebuffer());

	graphicsSystem->startRecording(CommandBufferType::Frame);
	{
		SET_GPU_DEBUG_LABEL("Shape AABB", Color::transparent);
		framebufferView->beginRenderPass(float4(0.0f));

		auto subType = shapeView->getSubType();
		if (subType == ShapeSubType::Box)
		{
			auto mvp = cameraConstants.viewProj * model * scale(shapeView->getBoxHalfExtent() * 2.0f);
			graphicsSystem->drawAabb(mvp, (float4)aabbColor);
		}

		framebufferView->endRenderPass();
	}
	graphicsSystem->stopRecording();
}

void PhysicsEditorSystem::editorRender()
{
	auto graphicsSystem = GraphicsSystem::get();
	auto editorSystem = EditorRenderSystem::get();
	if (!isEnabled || !editorSystem->selectedEntity || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto physicsSystem = PhysicsSystem::get();
	auto rigidbodyView = physicsSystem->tryGet(editorSystem->selectedEntity);

	if (rigidbodyView && rigidbodyView->getShape())
	{
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, rigidbodyView->getShape(), rigidbodyAabbColor);
	}

	auto characterSystem = CharacterSystem::get();
	auto characterView = characterSystem->tryGet(editorSystem->selectedEntity);

	if (characterView && characterView->getShape())
	{
		float3 position; quat rotation;
		characterView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, characterView->getShape(), characterAabbColor);
	}
}

//**********************************************************************************************************************
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView, const float3& rigidbodyShapePosCached, 
	float3& rigidbodyHalfExtCached, float& rigidbodyConvexRadCached, AllowedDOF allowedDofCache)
{
	auto physicsSystem = PhysicsSystem::get();
	auto shape = rigidbodyView->getShape();
	auto innerShape = shape;
	auto isChanged = false;

	auto shapeView = physicsSystem->get(shape);
	if (shapeView->getType() == ShapeType::Decorated)
	{
		innerShape = shapeView->getInnerShape();
		shapeView = physicsSystem->get(innerShape);
	}

	rigidbodyHalfExtCached = shapeView->getBoxHalfExtent();
	isChanged |= ImGui::DragFloat3("Half Extent", &rigidbodyHalfExtCached, 0.01f);
	if (ImGui::BeginPopupContextItem("halfExtent"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			rigidbodyHalfExtCached = float3(0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	rigidbodyConvexRadCached = shapeView->getBoxConvexRadius();
	isChanged |= ImGui::DragFloat("Convex Radius", &rigidbodyConvexRadCached, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			rigidbodyConvexRadCached = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		rigidbodyConvexRadCached = max(rigidbodyConvexRadCached, 0.0f);
		rigidbodyHalfExtCached = max(rigidbodyHalfExtCached, float3(rigidbodyConvexRadCached));

		physicsSystem->destroyShared(shape);
		if (rigidbodyShapePosCached != float3(0.0f))
		{
			innerShape = physicsSystem->createSharedBoxShape(rigidbodyHalfExtCached, rigidbodyConvexRadCached);
			shape = physicsSystem->createSharedRotTransShape(innerShape, rigidbodyShapePosCached);
		}
		else shape = physicsSystem->createSharedBoxShape(rigidbodyHalfExtCached, rigidbodyConvexRadCached);
		rigidbodyView->setShape(shape, false, false, rigidbodyView->isSensor(), allowedDofCache);
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView, float3& rigidbodyShapePosCached, 
	float3& rigidbodyHalfExtCached, float& rigidbodyConvexRadCached, AllowedDOF allowedDofCached)
{
	auto physicsSystem = PhysicsSystem::get();
	auto shape = rigidbodyView->getShape();
	auto innerShape = shape;
	auto isChanged = false;

	ImGui::SeparatorText("Shape");
	ImGui::PushID("shape");

	int shapeType = 0;
	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);

		quat _placeholder;
		if (shapeView->getSubType() == ShapeSubType::RotatedTranslated)
			shapeView->getPosAndRot(rigidbodyShapePosCached, _placeholder);

		if (shapeView->getType() == ShapeType::Decorated)
		{
			innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
		}

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

	isChanged |= ImGui::DragFloat3("Position", &rigidbodyShapePosCached, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			rigidbodyShapePosCached = float3(0.0f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	// TODO: shape rotation

	const auto sTypes = "None\0Custom\0Box\00";
	if (ImGui::Combo("Type", &shapeType, sTypes) || isChanged)
	{
		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			rigidbodyView->setShape({});
			break;
		case 2:
			physicsSystem->destroyShared(shape);
			if (rigidbodyShapePosCached != float3(0.0f))
			{
				innerShape = physicsSystem->createSharedBoxShape(float3(0.5f));
				shape = physicsSystem->createSharedRotTransShape(innerShape, rigidbodyShapePosCached);
			}
			else 
				shape = physicsSystem->createSharedBoxShape(float3(0.5f));
			rigidbodyView->setShape(shape, false, rigidbodyView->canBeKinematicOrDynamic(), 
				rigidbodyView->isSensor(), allowedDofCached);
			break;
		default:
			break;
		}
	}

	ImGui::PopID();

	if (shapeType == 2)
	{
		renderBoxShape(rigidbodyView, rigidbodyShapePosCached,
			rigidbodyHalfExtCached, rigidbodyConvexRadCached, allowedDofCached);
	}
}

//**********************************************************************************************************************
static void renderAdvancedProperties(View<RigidbodyComponent> rigidbodyView, AllowedDOF& allowedDofCached)
{
	ImGui::Indent();
	
	auto shape = rigidbodyView->getShape();
	auto isAnyChanged = false;

	ImGui::BeginDisabled(rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Degrees of Freedom (DOF)");
	allowedDofCached = rigidbodyView->getAllowedDOF();

	ImGui::Text("Translation:"); ImGui::SameLine();
	ImGui::PushID("dofTranslation");
	auto transX = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationX);
	isAnyChanged |= ImGui::Checkbox("X", &transX); ImGui::SameLine();
	auto transY = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationY);
	isAnyChanged |= ImGui::Checkbox("Y", &transY); ImGui::SameLine();
	auto transZ = hasAnyFlag(allowedDofCached, AllowedDOF::TranslationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &transZ);
	ImGui::PopID();

	ImGui::Text("Rotation:   "); ImGui::SameLine();
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
			rigidbodyView->setShape({});
			rigidbodyView->setShape(shape, false, rigidbodyView->canBeKinematicOrDynamic(),
				rigidbodyView->isSensor(), allowedDofCached);
		}
	}

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Velocity");
	
	auto velocity = rigidbodyView->getLinearVelocity();
	if (ImGui::DragFloat3("Linear", &velocity, 0.01f))
		rigidbodyView->setLinearVelocity(velocity);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Units: meters per second (m/s)");
		ImGui::EndTooltip();
	}
	if (ImGui::BeginPopupContextItem("linearVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setLinearVelocity(float3(0.0f));
		ImGui::EndPopup();
	}

	velocity = rigidbodyView->getAngularVelocity();
	if (ImGui::DragFloat3("Angular", &velocity, 0.01f))
		rigidbodyView->setAngularVelocity(velocity);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Units: radians per second (rad/s)");
		ImGui::EndTooltip();
	}
	if (ImGui::BeginPopupContextItem("angularVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setAngularVelocity(float3(0.0f));
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	ImGui::Unindent();
}

//**********************************************************************************************************************
static void renderEventListeners(View<RigidbodyComponent> rigidbodyView)
{
	const auto& listeners = rigidbodyView->getListeners();
	for (psize i = 0; i < listeners.size(); i++)
	{
		const auto& listener = listeners[i];
		auto name = typeToString(listener.systemType);
		switch (listener.eventType)
		{
		case BodyEvent::Activated:
			name += " -> Activated";
			break;
		case BodyEvent::Deactivated:
			name += " -> Deactivated";
			break;
		case BodyEvent::ContactAdded:
			name += " -> ContactAdded";
			break;
		case BodyEvent::ContactPersisted:
			name += " -> ContactPersisted";
			break;
		case BodyEvent::ContactRemoved:
			name += " -> ContactRemoved";
			break;
		default: abort();
		}
		ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	if (listeners.empty())
	{
		ImGui::Indent();
		ImGui::TextDisabled("No subscribed event listeners");
		ImGui::Unindent();
	}
}

//**********************************************************************************************************************
void PhysicsEditorSystem::onRigidbodyInspector(ID<Entity> entity, bool isOpened)
{
	auto physicsSystem = PhysicsSystem::get();
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
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!shape);
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

		if (ImGui::DragFloat3("Rotation", &newRigidbodyEulerAngles, 0.3f))
		{
			auto difference = newRigidbodyEulerAngles - oldRigidbodyEulerAngles;
			rotation *= quat(radians(difference));
			rigidbodyView->setRotation(normalize(rotation), false);
			oldRigidbodyEulerAngles = newRigidbodyEulerAngles;
		}
		if (ImGui::BeginPopupContextItem("rotation"))
		{
			if (ImGui::MenuItem("Reset Default"))
				rigidbodyView->setRotation(quat::identity, false);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			auto rotation = radians(newRigidbodyEulerAngles);
			ImGui::Text("Rotation in degrees\nRadians: %.3f, %.3f, %.3f",
				rotation.x, rotation.y, rotation.z);
			ImGui::EndTooltip();
		}
		ImGui::EndDisabled();

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

		renderShapeProperties(rigidbodyView, rigidbodyShapePosCached, 
			rigidbodyHalfExtCached, rigidbodyConvexRadCached, allowedDofCached);
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Advanced Properties"))
			renderAdvancedProperties(rigidbodyView, allowedDofCached);
		if (ImGui::CollapsingHeader("Event Listeners"))
			renderEventListeners(rigidbodyView);
	}

	auto editorSystem = EditorRenderSystem::get();
	if (editorSystem->selectedEntity != selectedEntity)
	{
		if (editorSystem->selectedEntity)
		{
			auto rigidbodyView = physicsSystem->get(editorSystem->selectedEntity);
			if (rigidbodyView->getShape())
			{
				auto rotation = rigidbodyView->getRotation();
				oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.toEulerAngles());
				oldRigidbodyRotation = rotation;
			}
		}

		selectedEntity = editorSystem->selectedEntity;
		rigidbodyShapePosCached = float3(0.0f);
		rigidbodyHalfExtCached = float3(0.5f);
		rigidbodyConvexRadCached = 0.05f;
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
				if (oldRigidbodyRotation != rotation)
				{
					oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.toEulerAngles());
					oldRigidbodyRotation = rotation;
				}
			}
		}
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<CharacterComponent> characterView, float3& characterShapePosCached, 
	float3& characterShapeSizeCached, float& characterConvexRadCached)
{
	auto physicsSystem = PhysicsSystem::get();
	auto shape = characterView->getShape();
	auto isChanged = false;

	ImGui::SeparatorText("Shape");
	ImGui::PushID("shape");

	int shapeType = 0;
	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);
		if (shapeView->getType() == ShapeType::Decorated)
		{
			quat _placeholder;
			if (shapeView->getSubType() == ShapeSubType::RotatedTranslated)
				shapeView->getPosAndRot(characterShapePosCached, _placeholder);

			auto innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
			switch (shapeView->getSubType())
			{
			case ShapeSubType::Box:
				characterShapeSizeCached = shapeView->getBoxHalfExtent() * 2.0f;
				characterConvexRadCached = shapeView->getBoxConvexRadius();
				shapeType = 2;
				break;
			default:
				shapeType = 1;
				break;
			}
		}
		else
		{
			shapeType = 1;
		}
	}

	isChanged |= ImGui::DragFloat3("Size", &characterShapeSizeCached, 0.01f);
	if (ImGui::BeginPopupContextItem("shapeSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			characterShapeSizeCached = float3(0.5f, 1.75f, 0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	isChanged |= ImGui::DragFloat("Convex Radius", &characterConvexRadCached, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			characterConvexRadCached = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	isChanged |= ImGui::DragFloat3("Position", &characterShapePosCached, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			characterShapePosCached = float3(0.0f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	// TODO: shape rotation

	ID<Shape> innerShape = {};

	const auto sTypes = "None\0Custom\0Box\00";
	if ((ImGui::Combo("Type", &shapeType, sTypes) || isChanged))
	{
		characterConvexRadCached = max(characterConvexRadCached, 0.0f);
		characterShapeSizeCached = max(characterShapeSizeCached, float3(characterConvexRadCached * 2.0f));

		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			characterView->setShape({});
			break;
		case 2:
			physicsSystem->destroyShared(shape);
			innerShape = physicsSystem->createSharedBoxShape(characterShapeSizeCached * 0.5f, characterConvexRadCached);
			shape = physicsSystem->createRotTransShape(innerShape, float3(characterShapePosCached.x,
				characterShapePosCached.y + characterShapeSizeCached.y * 0.5f, characterShapePosCached.z));
			characterView->setShape(shape);
			break;
		default:
			break;
		}
	}

	ImGui::PopID();
}

//**********************************************************************************************************************
static void renderAdvancedProperties(View<CharacterComponent> characterView)
{
	ImGui::Indent();
	auto shape = characterView->getShape();

	ImGui::BeginDisabled(!shape);
	auto velocity = characterView->getLinearVelocity();
	if (ImGui::DragFloat3("Linear Velocity", &velocity, 0.01f))
		characterView->setLinearVelocity(velocity);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Units: meters per second (m/s)");
		ImGui::EndTooltip();
	}
	if (ImGui::BeginPopupContextItem("linearVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			characterView->setLinearVelocity(float3(0.0f));
		ImGui::EndPopup();
	}

	string stateString = "";
	auto groundState = characterView->getGroundState();
	if (groundState == CharacterGround::OnGround)
		stateString = "OnGround";
	else if (groundState == CharacterGround::OnSteepGround)
		stateString = "OnSteepGround";
	else if (groundState == CharacterGround::NotSupported)
		stateString = "NotSupported";
	else if (groundState == CharacterGround::InAir)
		stateString = "InAir";
	else abort();
	ImGui::InputText("State", &stateString, ImGuiInputTextFlags_ReadOnly);
	ImGui::EndDisabled();

	ImGui::Unindent();
}

//**********************************************************************************************************************
void PhysicsEditorSystem::onCharacterInspector(ID<Entity> entity, bool isOpened)
{
	auto characterSystem = CharacterSystem::get();
	if (ImGui::BeginItemTooltip())
	{
		auto characterView = characterSystem->get(entity);
		ImGui::Text("Has shape: %s", characterView->getShape() ? "true" : "false");
		ImGui::EndTooltip();
	}

	if (isOpened)
	{
		auto characterView = characterSystem->get(entity);
		auto shape = characterView->getShape();

		ImGui::BeginDisabled(!shape);
		float3 position; quat rotation;
		characterView->getPosAndRot(position, rotation);
		if (ImGui::DragFloat3("Position", &position, 0.01f))
			characterView->setPosition(position);
		if (ImGui::BeginPopupContextItem("position"))
		{
			if (ImGui::MenuItem("Reset Default"))
				characterView->setPosition(float3(0.0f));
			ImGui::EndPopup();
		}

		if (ImGui::DragFloat3("Rotation", &newCharacterEulerAngles, 0.3f))
		{
			auto difference = newCharacterEulerAngles - oldCharacterEulerAngles;
			rotation *= quat(radians(difference));
			characterView->setRotation(rotation);
			oldCharacterEulerAngles = newCharacterEulerAngles;
		}
		if (ImGui::BeginPopupContextItem("rotation"))
		{
			if (ImGui::MenuItem("Reset Default"))
				characterView->setRotation(quat::identity);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			auto rotation = radians(newCharacterEulerAngles);
			ImGui::Text("Rotation in degrees\nRadians: %.3f, %.3f, %.3f",
				rotation.x, rotation.y, rotation.z);
			ImGui::EndTooltip();
		}

		auto mass = characterView->getMass();
		if (ImGui::DragFloat("Mass", &mass, 0.01f))
			characterView->setMass(mass);
		if (ImGui::BeginPopupContextItem("mass"))
		{
			if (ImGui::MenuItem("Reset Default"))
				characterView->setMass(70.0f);
			ImGui::EndPopup();
		}
		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Units: kilograms");
			ImGui::EndTooltip();
		}
		ImGui::EndDisabled();

		renderShapeProperties(characterView, characterShapePosCached,
			characterShapeSizeCached, characterConvexRadCached);

		if (ImGui::CollapsingHeader("Advanced Properties"))
			renderAdvancedProperties(characterView);
	}

	auto editorSystem = EditorRenderSystem::get();
	if (editorSystem->selectedEntity != selectedEntity)
	{
		if (editorSystem->selectedEntity)
		{
			auto characterView = characterSystem->get(editorSystem->selectedEntity);
			if (characterView->getShape())
			{
				auto rotation = characterView->getRotation();
				oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.toEulerAngles());
				oldCharacterRotation = rotation;
			}
		}

		selectedEntity = editorSystem->selectedEntity;
		characterShapeSizeCached = float3(0.5f, 1.75f, 0.5f);
		characterShapePosCached = float3(0.0f);
	}
	else
	{
		if (editorSystem->selectedEntity)
		{
			auto characterView = characterSystem->get(editorSystem->selectedEntity);
			if (characterView->getShape())
			{
				auto rotation = characterView->getRotation();
				if (oldCharacterRotation != rotation)
				{
					oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.toEulerAngles());
					oldCharacterRotation = rotation;
				}
			}
		}
	}
}
#endif
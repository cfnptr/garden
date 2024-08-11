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
#include "garden/system/transform.hpp"
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
	auto selectedEntity = editorSystem->selectedEntity;
	if (!isEnabled || !selectedEntity || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto physicsSystem = PhysicsSystem::get();
	auto rigidbodyView = physicsSystem->tryGet(selectedEntity);

	if (rigidbodyView && rigidbodyView->getShape())
	{
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, rigidbodyView->getShape(), rigidbodyAabbColor);
	}

	auto characterSystem = CharacterSystem::get();
	auto characterView = characterSystem->tryGet(selectedEntity);

	if (characterView && characterView->getShape())
	{
		float3 position; quat rotation;
		characterView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, characterView->getShape(), characterAabbColor);
	}
}

//**********************************************************************************************************************
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView, const float3& rigidbodyShapePosCached, 
	float3& rigidbodyHalfExtCached, float& rigidbodyConvexRadCached, float& rigidbodyDensityCached, 
	AllowedDOF allowedDofCache, bool isChanged)
{
	auto physicsSystem = PhysicsSystem::get();
	auto shape = rigidbodyView->getShape();

	View<Shape> shapeView = {};
	if (shape)
	{
		shapeView = physicsSystem->get(shape);
		if (shapeView->getType() == ShapeType::Decorated)
		{
			auto innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
		}
	}

	if (shape)
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

	if (shape)
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

	if (shape)
		rigidbodyDensityCached = shapeView->getDensity();
	isChanged |= ImGui::DragFloat("Density", &rigidbodyDensityCached, 1.0f);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Units: kilograms per cubic metre (kg / m^3)");
		ImGui::EndTooltip();
	}
	if (ImGui::BeginPopupContextItem("density"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			rigidbodyDensityCached = 1000.0f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		rigidbodyConvexRadCached = max(rigidbodyConvexRadCached, 0.0f);
		rigidbodyHalfExtCached = max(rigidbodyHalfExtCached, float3(rigidbodyConvexRadCached));
		rigidbodyDensityCached = max(rigidbodyDensityCached, 0.001f);

		physicsSystem->destroyShared(shape);
		if (rigidbodyShapePosCached != float3(0.0f))
		{
			auto innerShape = physicsSystem->createSharedBoxShape(
				rigidbodyHalfExtCached, rigidbodyConvexRadCached, rigidbodyDensityCached);
			shape = physicsSystem->createSharedRotTransShape(innerShape, rigidbodyShapePosCached);
		}
		else
		{
			shape = physicsSystem->createSharedBoxShape(
				rigidbodyHalfExtCached, rigidbodyConvexRadCached, rigidbodyDensityCached);
		}
		rigidbodyView->setShape(shape, false, rigidbodyView->canBeKinematicOrDynamic(), allowedDofCache);
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView, float3& rigidbodyShapePosCached, 
	float3& rigidbodyHalfExtCached, float& rigidbodyConvexRadCached, 
	float& rigidbodyDensityCached, AllowedDOF allowedDofCached)
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

	ImGui::PopID();

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
			isChanged = true;
			break;
		default:
			break;
		}
	}

	if (shapeType == 2)
	{
		renderBoxShape(rigidbodyView, rigidbodyShapePosCached, rigidbodyHalfExtCached, 
			rigidbodyConvexRadCached, rigidbodyDensityCached, allowedDofCached, isChanged);
	}
}

//**********************************************************************************************************************
static void renderConstraints(View<RigidbodyComponent> rigidbodyView,
	ID<Entity>& constraintTargetCached, ConstraintType& constraintTypeCached)
{
	ImGui::Indent();
	ImGui::BeginDisabled(!rigidbodyView->getShape());

	const auto cTypes = "Fixed\00";
	ImGui::Combo("Type", &constraintTypeCached, cTypes);

	auto name = constraintTargetCached ? "Entity " + to_string(*constraintTargetCached) : "";
	if (constraintTargetCached)
	{
		auto transformView = Manager::get()->tryGet<TransformComponent>(constraintTargetCached);
		if (transformView && !transformView->debugName.empty())
			name = transformView->debugName;
	}

	ImGui::InputText("Other Entity", &name, ImGuiInputTextFlags_ReadOnly);
	if (ImGui::BeginPopupContextItem("otherEntity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			constraintTargetCached = {};
		if (ImGui::MenuItem("Select Entity"))
			EditorRenderSystem::get()->selectedEntity = constraintTargetCached;
		ImGui::EndPopup();
	}
	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
			constraintTargetCached = *((const ID<Entity>*)payload->Data);
		}
		ImGui::EndDragDropTarget();
	}

	auto canCreate = !constraintTargetCached;
	if (constraintTargetCached && constraintTargetCached != rigidbodyView->getEntity())
	{
		auto otherView = PhysicsSystem::get()->tryGet(constraintTargetCached);
		if (otherView && otherView->getShape())
			canCreate = true;
	}

	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create Constraint", ImVec2(-FLT_MIN, 0.0f)))
		rigidbodyView->createConstraint(constraintTargetCached, constraintTypeCached);
	ImGui::EndDisabled();

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Created"))
	{
		ImGui::Indent();
		auto& constraints = rigidbodyView->getConstraints();
		for (uint32 i = 0; i < (uint32)constraints.size(); i++)
		{
			auto& constraint = constraints[i];
			ImGui::SeparatorText(to_string(i).c_str()); ImGui::SameLine();
			ImGui::PushID(to_string(i).c_str());

			if (ImGui::SmallButton(" - "))
			{
				rigidbodyView->destroyConstraint(i);
				ImGui::PopID();
				break;
			}

			auto type = constraint.type;
			ImGui::Combo("Type", &type, cTypes);

			auto transformView = Manager::get()->tryGet<TransformComponent>(constraint.otherBody);
			if (transformView && !transformView->debugName.empty())
				name = transformView->debugName;
			else
				name = "Entity " + to_string(*constraint.otherBody);
			ImGui::InputText("Other Entity", &name, ImGuiInputTextFlags_ReadOnly);
			if (ImGui::BeginPopupContextItem("otherEntity"))
			{
				if (ImGui::MenuItem("Select Entity"))
					EditorRenderSystem::get()->selectedEntity = constraintTargetCached;
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		if (constraints.empty())
			ImGui::TextDisabled("No created constraints");

		ImGui::Unindent();
	}

	ImGui::EndDisabled();
	ImGui::Unindent();
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
			rigidbodyView->setShape(shape, false, rigidbodyView->canBeKinematicOrDynamic(), allowedDofCached);
		}
	}

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Velocity");

	auto velocity = rigidbodyView->getLinearVelocity();
	if (ImGui::DragFloat3("Linear", &velocity, 0.01f))
		rigidbodyView->setLinearVelocity(velocity);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("Units: metres per second (m/s)");
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

	ImGui::Spacing();

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() != MotionType::Kinematic);
	auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();
	if (ImGui::Checkbox("Kinematic VS Static", &isKinematicVsStatic))
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	ImGui::EndDisabled();

	ImGui::Unindent();
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

	if (entity != rigidbodySelectedEntity)
	{
		if (entity)
		{
			auto rigidbodyView = physicsSystem->get(entity);
			if (rigidbodyView->getShape())
			{
				auto rotation = rigidbodyView->getRotation();
				oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.toEulerAngles());
				oldRigidbodyRotation = rotation;
			}
		}

		rigidbodySelectedEntity = entity;
		rigidbodyShapePosCached = float3(0.0f);
		rigidbodyHalfExtCached = float3(0.5f);
		rigidbodyConvexRadCached = 0.05f;
		rigidbodyDensityCached = 1000.0f;
		constraintTargetCached = {};
		constraintTypeCached = {};
		allowedDofCached = AllowedDOF::All;
	}
	else
	{
		if (entity)
		{
			auto rigidbodyView = physicsSystem->get(entity);
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

	if (!isOpened)
		return;

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

	ImGui::BeginDisabled(!isSensor);
	ImGui::InputText("Event Listener", &rigidbodyView->eventListener);
	if (ImGui::BeginPopupContextItem("eventListener"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->eventListener = "";
		ImGui::EndPopup();
	}
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

	renderShapeProperties(rigidbodyView, rigidbodyShapePosCached, rigidbodyHalfExtCached,
		rigidbodyConvexRadCached, rigidbodyDensityCached, allowedDofCached);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Constraints"))
		renderConstraints(rigidbodyView, constraintTargetCached, constraintTypeCached);
	if (ImGui::CollapsingHeader("Advanced Properties"))
		renderAdvancedProperties(rigidbodyView, allowedDofCached);
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

	const auto sTypes = "None\0Custom\0Box\00";
	if ((ImGui::Combo("Type", &shapeType, sTypes) || isChanged))
	{
		characterConvexRadCached = max(characterConvexRadCached, 0.0f);
		characterShapeSizeCached = max(characterShapeSizeCached, float3(characterConvexRadCached * 2.0f));
		ID<Shape> innerShape = {};

		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			characterView->setShape({});
			break;
		case 2:
			physicsSystem->destroyShared(shape);
			innerShape = physicsSystem->createSharedBoxShape(
				characterShapeSizeCached * 0.5f, characterConvexRadCached);
			shape = physicsSystem->createRotTransShape(innerShape, float3(
				characterShapePosCached.x, characterShapePosCached.y, characterShapePosCached.z));
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
		ImGui::Text("Units: metres per second (m/s)");
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

	if (entity != characterSelectedEntity)
	{
		if (entity)
		{
			auto characterView = characterSystem->get(entity);
			if (characterView->getShape())
			{
				auto rotation = characterView->getRotation();
				oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.toEulerAngles());
				oldCharacterRotation = rotation;
			}
		}

		characterSelectedEntity = entity;
		characterShapePosCached = float3(0.0f);
		characterShapeSizeCached = float3(0.5f, 1.75f, 0.5f);
		characterConvexRadCached = 0.05f;
	}
	else
	{
		if (entity)
		{
			auto characterView = characterSystem->get(entity);
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

	if (!isOpened)
		return;

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
#endif
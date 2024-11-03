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
#include "math/matrix/transform.hpp"
#include "math/angles.hpp"

using namespace garden;

//**********************************************************************************************************************
PhysicsEditorSystem::PhysicsEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", PhysicsEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", PhysicsEditorSystem::deinit);
	
}
PhysicsEditorSystem::~PhysicsEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", PhysicsEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", PhysicsEditorSystem::deinit);
	}
}

void PhysicsEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("EditorRender", PhysicsEditorSystem::editorRender);

	EditorRenderSystem::Instance::get()->registerEntityInspector<RigidbodyComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onRigidbodyInspector(entity, isOpened);
	},
	rigidbodyInspectorPriority);

	if (CharacterSystem::Instance::has())
	{
		EditorRenderSystem::Instance::get()->registerEntityInspector<CharacterComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCharacterInspector(entity, isOpened);
		},
		characterInspectorPriority);
	}
}
void PhysicsEditorSystem::deinit()
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	editorSystem->unregisterEntityInspector<RigidbodyComponent>();
	editorSystem->tryUnregisterEntityInspector<CharacterComponent>();

	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorRender", PhysicsEditorSystem::editorRender);
}

//**********************************************************************************************************************
static void renderShapeAABB(float3 position, quat rotation, ID<Shape> shape, Color aabbColor)
{
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

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
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto editorSystem = EditorRenderSystem::Instance::get();
	auto selectedEntity = editorSystem->selectedEntity;
	if (!isEnabled || !selectedEntity || !graphicsSystem->canRender() || !graphicsSystem->camera)
		return;

	auto physicsSystem = PhysicsSystem::Instance::get();
	auto rigidbodyView = physicsSystem->tryGetComponent(selectedEntity);

	if (rigidbodyView && rigidbodyView->getShape())
	{
		float3 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, rigidbodyView->getShape(), rigidbodyAabbColor);
	}

	auto characterSystem = CharacterSystem::Instance::get();
	auto characterView = characterSystem->tryGetComponent(selectedEntity);

	if (characterView && characterView->getShape())
	{
		float3 position; quat rotation;
		characterView->getPosAndRot(position, rotation);
		renderShapeAABB(position, rotation, characterView->getShape(), characterAabbColor);
	}
}

//**********************************************************************************************************************
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::Cached& cached, bool isChanged)
{
	auto physicsSystem = PhysicsSystem::Instance::get();
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
		cached.rigidbodyHalfExt = shapeView->getBoxHalfExtent();
	isChanged |= ImGui::DragFloat3("Half Extent", &cached.rigidbodyHalfExt, 0.01f);
	if (ImGui::BeginPopupContextItem("halfExtent"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.rigidbodyHalfExt = float3(0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cached.rigidbodyConvexRad = shapeView->getBoxConvexRadius();
	isChanged |= ImGui::DragFloat("Convex Radius", &cached.rigidbodyConvexRad, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.rigidbodyConvexRad = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cached.rigidbodyDensity = shapeView->getDensity();
	isChanged |= ImGui::DragFloat("Density", &cached.rigidbodyDensity, 1.0f, 0.0f, 0.0f, "%.3f kg/m^3");
	if (ImGui::BeginPopupContextItem("density"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.rigidbodyDensity = 1000.0f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		cached.rigidbodyConvexRad = max(cached.rigidbodyConvexRad, 0.0f);
		cached.rigidbodyHalfExt = max(cached.rigidbodyHalfExt, float3(cached.rigidbodyConvexRad));
		cached.rigidbodyDensity = max(cached.rigidbodyDensity, 0.001f);

		if (cached.allowedDOF == AllowedDOF::None && cached.motionType != MotionType::Static)
			cached.motionType = MotionType::Static;

		auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();
		physicsSystem->destroyShared(shape);
		
		if (cached.rigidbodyShapePos != float3(0.0f))
		{
			auto innerShape = physicsSystem->createSharedBoxShape(
				cached.rigidbodyHalfExt, cached.rigidbodyConvexRad, cached.rigidbodyDensity);
			shape = physicsSystem->createSharedRotTransShape(innerShape, cached.rigidbodyShapePos);
		}
		else
		{
			shape = physicsSystem->createSharedBoxShape(cached.rigidbodyHalfExt, 
				cached.rigidbodyConvexRad, cached.rigidbodyDensity);
		}

		rigidbodyView->setShape(shape, cached.motionType, cached.rigidbodyCollLayer,
			false, rigidbodyView->canBeKinematicOrDynamic(), cached.allowedDOF);
		rigidbodyView->setSensor(cached.isSensor);
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::Cached& cached)
{
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto shape = rigidbodyView->getShape();
	auto innerShape = shape;
	auto isChanged = false;

	ImGui::SeparatorText("Shape");
	ImGui::PushID("shape");

	int shapeType = 0;
	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);

		if (shapeView->getSubType() == ShapeSubType::RotatedTranslated)
			cached.rigidbodyShapePos = shapeView->getPosition();

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

	isChanged |= ImGui::DragFloat3("Position", &cached.rigidbodyShapePos, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.rigidbodyShapePos = float3(0.0f);
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
		renderBoxShape(rigidbodyView, cached, isChanged);
}

//**********************************************************************************************************************
static void renderConstraints(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::Cached& cached)
{
	ImGui::Indent();
	ImGui::PushID("constraints");

	ImGui::BeginDisabled(!rigidbodyView->getShape());
	const auto cTypes = "Fixed\0Point\00";
	ImGui::Combo("Type", &cached.constraintType, cTypes);

	auto name = cached.constraintTarget ? "Entity " + to_string(*cached.constraintTarget) : "";
	if (cached.constraintTarget)
	{
		auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(cached.constraintTarget);
		if (transformView && !transformView->debugName.empty())
			name = transformView->debugName;
	}

	ImGui::InputText("Other Entity", &name, ImGuiInputTextFlags_ReadOnly);
	if (ImGui::BeginPopupContextItem("otherEntity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cached.constraintTarget = {};
		if (ImGui::MenuItem("Select Entity"))
			EditorRenderSystem::Instance::get()->selectedEntity = cached.constraintTarget;
		ImGui::EndPopup();
	}
	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
			cached.constraintTarget = *((const ID<Entity>*)payload->Data);
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::Checkbox("Auto Points", &cached.autoConstraintPoints);

	ImGui::BeginDisabled(cached.autoConstraintPoints);
	ImGui::DragFloat3("This Point", &cached.thisConstraintPoint, 0.01f);
	if (ImGui::BeginPopupContextItem("thisPoint"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cached.thisConstraintPoint = float3(0.0f);
		ImGui::EndPopup();
	}
	ImGui::DragFloat3("Other Point", &cached.otherConstraintPoint, 0.01f);
	if (ImGui::BeginPopupContextItem("otherPoint"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cached.otherConstraintPoint = float3(0.0f);
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	auto canCreate = !cached.constraintTarget;
	if (cached.constraintTarget && cached.constraintTarget != rigidbodyView->getEntity())
	{
		auto otherView = PhysicsSystem::Instance::get()->tryGetComponent(cached.constraintTarget);
		if (otherView && otherView->getShape())
			canCreate = true;
	}

	ImGui::Separator();

	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create Constraint", ImVec2(-FLT_MIN, 0.0f)))
	{
		rigidbodyView->createConstraint(cached.constraintTarget, cached.constraintType,
			cached.autoConstraintPoints ? float3(FLT_MAX) : cached.thisConstraintPoint, 
			cached.autoConstraintPoints ? float3(FLT_MAX) : cached.otherConstraintPoint);
	}
	ImGui::EndDisabled();

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Created"))
	{
		ImGui::Indent();
		ImGui::PushID("constraint");

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

			if (constraint.otherBody)
			{
				auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(constraint.otherBody);
				if (transformView && !transformView->debugName.empty())
					name = transformView->debugName;
				else
					name = "Entity " + to_string(*constraint.otherBody);
			}
			else
			{
				name = "<world>";
			}
			
			ImGui::InputText("Other Entity", &name, ImGuiInputTextFlags_ReadOnly);
			if (ImGui::BeginPopupContextItem("otherEntity"))
			{
				if (ImGui::MenuItem("Select Entity"))
					EditorRenderSystem::Instance::get()->selectedEntity = cached.constraintTarget;
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		if (constraints.empty())
			ImGui::TextDisabled("No created constraints");

		ImGui::PopID();
		ImGui::Unindent();
	}
	ImGui::EndDisabled();

	ImGui::PopID();
	ImGui::Unindent();
}

//**********************************************************************************************************************
static void renderAdvancedProperties(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::Cached& cached)
{
	ImGui::Indent();
	
	auto shape = rigidbodyView->getShape();
	auto isAnyChanged = false;

	ImGui::BeginDisabled(rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Degrees of Freedom (DOF)");

	if (shape)
		cached.allowedDOF = rigidbodyView->getAllowedDOF();

	ImGui::SameLine();
	if (ImGui::SmallButton("Set 3D"))
	{
		cached.allowedDOF = AllowedDOF::All;
		isAnyChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Set 2D"))
	{
		cached.allowedDOF = AllowedDOF::TranslationX | 
			AllowedDOF::TranslationY | AllowedDOF::RotationZ;
		isAnyChanged = true;
	}

	ImGui::Text("Translation:"); ImGui::SameLine();
	ImGui::PushID("dofTranslation");
	auto transX = hasAnyFlag(cached.allowedDOF, AllowedDOF::TranslationX);
	isAnyChanged |= ImGui::Checkbox("X", &transX); ImGui::SameLine();
	auto transY = hasAnyFlag(cached.allowedDOF, AllowedDOF::TranslationY);
	isAnyChanged |= ImGui::Checkbox("Y", &transY); ImGui::SameLine();
	auto transZ = hasAnyFlag(cached.allowedDOF, AllowedDOF::TranslationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &transZ);
	ImGui::PopID();

	ImGui::Text("Rotation:   "); ImGui::SameLine();
	ImGui::PushID("dofRotation");
	auto rotX = hasAnyFlag(cached.allowedDOF, AllowedDOF::RotationX);
	isAnyChanged |= ImGui::Checkbox("X", &rotX); ImGui::SameLine();
	auto rotY = hasAnyFlag(cached.allowedDOF, AllowedDOF::RotationY);
	isAnyChanged |= ImGui::Checkbox("Y", &rotY); ImGui::SameLine();
	auto rotZ = hasAnyFlag(cached.allowedDOF, AllowedDOF::RotationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &rotZ);
	ImGui::PopID();
	ImGui::EndDisabled();

	if (isAnyChanged)
	{
		cached.allowedDOF = AllowedDOF::None;
		if (transX) cached.allowedDOF |= AllowedDOF::TranslationX;
		if (transY) cached.allowedDOF |= AllowedDOF::TranslationY;
		if (transZ) cached.allowedDOF |= AllowedDOF::TranslationZ;
		if (rotX) cached.allowedDOF |= AllowedDOF::RotationX;
		if (rotY) cached.allowedDOF |= AllowedDOF::RotationY;
		if (rotZ) cached.allowedDOF |= AllowedDOF::RotationZ;

		if (shape)
		{
			auto motionType = rigidbodyView->getMotionType();
			auto collisionLayer = rigidbodyView->getCollisionLayer();
			auto canBeKinematicOrDynamic = rigidbodyView->canBeKinematicOrDynamic();
			auto isSensor = rigidbodyView->isSensor();
			auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();
			
			rigidbodyView->setShape({});
			rigidbodyView->setShape(shape, motionType, collisionLayer, 
				false, canBeKinematicOrDynamic, cached.allowedDOF);
			rigidbodyView->setSensor(isSensor);
			rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
		}
	}

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Velocity");

	auto velocity = rigidbodyView->getLinearVelocity();
	if (ImGui::DragFloat3("Linear", &velocity, 0.01f, 0.0f, 0.0f, "%.3f m/s"))
		rigidbodyView->setLinearVelocity(velocity);
	if (ImGui::BeginPopupContextItem("linearVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setLinearVelocity(float3(0.0f));
		ImGui::EndPopup();
	}

	velocity = rigidbodyView->getAngularVelocity();
	if (ImGui::DragFloat3("Angular", &velocity, 0.01f, 0.0f, 0.0f, "%.3f rad/s"))
		rigidbodyView->setAngularVelocity(velocity);
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
	auto physicsSystem = PhysicsSystem::Instance::get();
	if (ImGui::BeginItemTooltip())
	{
		auto rigidbodyView = physicsSystem->getComponent(entity);
		ImGui::Text("Has shape: %s, Active: %s", rigidbodyView->getShape() ? "true" : "false",
			rigidbodyView->isActive() ? "true" : "false");
		auto motionType = rigidbodyView->getMotionType();
		if (motionType == MotionType::Static)
			ImGui::Text("Motion type: Static");
		else if (motionType == MotionType::Kinematic)
			ImGui::Text("Motion type: Kinematic");
		else if (motionType == MotionType::Dynamic)
			ImGui::Text("Motion type: Dynamic");
		else abort();
		ImGui::EndTooltip();
	}

	if (entity != rigidbodySelectedEntity)
	{
		if (entity)
		{
			auto rigidbodyView = physicsSystem->getComponent(entity);
			if (rigidbodyView->getShape())
			{
				auto rotation = rigidbodyView->getRotation();
				oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.toEulerAngles());
				oldRigidbodyRotation = rotation;
			}
		}

		rigidbodySelectedEntity = entity;
		cached.rigidbodyShapePos = float3(0.0f);
		cached.rigidbodyHalfExt = float3(0.5f);
		cached.rigidbodyConvexRad = 0.05f;
		cached.rigidbodyDensity = 1000.0f;
		cached.rigidbodyCollLayer = -1;
		cached.thisConstraintPoint = float3(0.0f);
		cached.otherConstraintPoint = float3(0.0f);
		cached.autoConstraintPoints = true;
		cached.isSensor = false;
		cached.motionType = {};
		cached.constraintTarget = {};
		cached.constraintType = {};
		cached.allowedDOF = AllowedDOF::All;
	}
	else
	{
		if (entity)
		{
			auto rigidbodyView = physicsSystem->getComponent(entity);
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

	auto rigidbodyView = physicsSystem->getComponent(entity);
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
	if (shape)
		cached.isSensor = rigidbodyView->isSensor();
	if (ImGui::Checkbox("Sensor", &cached.isSensor))
		rigidbodyView->setSensor(cached.isSensor);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!cached.isSensor);
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

	if (shape)
		cached.rigidbodyCollLayer = (int)rigidbodyView->getCollisionLayer();
	if (ImGui::DragInt("Collision Layer", &cached.rigidbodyCollLayer))
		rigidbodyView->setCollisionLayer(cached.rigidbodyCollLayer);
	if (ImGui::BeginPopupContextItem("collisionLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setCollisionLayer();
		ImGui::EndPopup();
	}
	if (ImGui::BeginItemTooltip())
	{
		switch (rigidbodyView->getCollisionLayer())
		{
		case (uint16)CollisionLayer::NonMoving:
			ImGui::Text("Collision Layer: Non Moving");
			break;
		case (uint16)CollisionLayer::Moving:
			ImGui::Text("Collision Layer: Moving");
			break;
		case (uint16)CollisionLayer::Sensor:
			ImGui::Text("Collision Layer: Sensor");
			break;
		case (uint16)CollisionLayer::HqDebris:
			ImGui::Text("Collision Layer: High Quality Debris");
			break;
		case (uint16)CollisionLayer::LqDebris:
			ImGui::Text("Collision Layer: Low Quality Debris");
			break;
		default:
			ImGui::Text("Collision Layer: Custom");
			break;
		}
		ImGui::EndTooltip();
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(shape && !rigidbodyView->canBeKinematicOrDynamic());
	const auto mTypes = "Static\0Kinematic\0Dynamic\00";
	if (shape)
		cached.motionType = rigidbodyView->getMotionType();
	if (ImGui::Combo("Motion Type", &cached.motionType, mTypes))
	{
		if (cached.motionType != MotionType::Static)
			cached.allowedDOF = AllowedDOF::All;
		cached.rigidbodyCollLayer = -1;
		rigidbodyView->setMotionType(cached.motionType);
	}
	ImGui::EndDisabled();

	renderShapeProperties(rigidbodyView, cached);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Constraints"))
		renderConstraints(rigidbodyView, cached);
	if (ImGui::CollapsingHeader("Advanced Properties"))
		renderAdvancedProperties(rigidbodyView, cached);
}

//**********************************************************************************************************************
static void renderShapeProperties(View<CharacterComponent> characterView, PhysicsEditorSystem::Cached& cached)
{
	auto physicsSystem = PhysicsSystem::Instance::get();
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
			if (shapeView->getSubType() == ShapeSubType::RotatedTranslated)
				cached.characterShapePos = shapeView->getPosition();

			auto innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
			switch (shapeView->getSubType())
			{
			case ShapeSubType::Box:
				cached.characterShapeSize = shapeView->getBoxHalfExtent() * 2.0f;
				cached.characterConvexRad = shapeView->getBoxConvexRadius();
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

	isChanged |= ImGui::DragFloat3("Size", &cached.characterShapeSize, 0.01f);
	if (ImGui::BeginPopupContextItem("shapeSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.characterShapeSize = float3(0.5f, 1.75f, 0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	isChanged |= ImGui::DragFloat("Convex Radius", &cached.characterConvexRad, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.characterConvexRad = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	isChanged |= ImGui::DragFloat3("Position", &cached.characterShapePos, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cached.characterShapePos = float3(0.0f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	// TODO: shape rotation

	const auto sTypes = "None\0Custom\0Box\00";
	if ((ImGui::Combo("Type", &shapeType, sTypes) || isChanged))
	{
		cached.characterConvexRad = max(cached.characterConvexRad, 0.0f);
		cached.characterShapeSize = max(cached.characterShapeSize, float3(cached.characterConvexRad * 2.0f));
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
				cached.characterShapeSize * 0.5f, cached.characterConvexRad);
			shape = physicsSystem->createRotTransShape(innerShape, cached.characterShapePos);
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
	if (ImGui::DragFloat3("Linear Velocity", &velocity, 0.01f, 0.0f, 0.0f, "%.3f m/s"))
		characterView->setLinearVelocity(velocity);
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
	auto characterSystem = CharacterSystem::Instance::get();
	if (ImGui::BeginItemTooltip())
	{
		auto characterView = characterSystem->getComponent(entity);
		ImGui::Text("Has shape: %s", characterView->getShape() ? "true" : "false");
		ImGui::EndTooltip();
	}

	if (entity != characterSelectedEntity)
	{
		if (entity)
		{
			auto characterView = characterSystem->getComponent(entity);
			if (characterView->getShape())
			{
				auto rotation = characterView->getRotation();
				oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.toEulerAngles());
				oldCharacterRotation = rotation;
			}
		}

		characterSelectedEntity = entity;
		cached.characterShapePos = float3(0.0f);
		cached.characterShapeSize = float3(0.5f, 1.75f, 0.5f);
		cached.characterConvexRad = 0.05f;
	}
	else
	{
		if (entity)
		{
			auto characterView = characterSystem->getComponent(entity);
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

	auto characterView = characterSystem->getComponent(entity);
	auto shape = characterView->getShape();

	ImGui::BeginDisabled(!shape);
	auto collisionLayer = (int)characterView->collisionLayer;
	if (ImGui::DragInt("Collision Layer", &collisionLayer))
	{
		auto physicsSystem = PhysicsSystem::Instance::get();
		if (collisionLayer < 0 || collisionLayer >= physicsSystem->getProperties().collisionLayerCount)
			collisionLayer = (uint16)CollisionLayer::Moving;
		characterView->collisionLayer = (uint16)collisionLayer;
	}
	if (ImGui::BeginPopupContextItem("collisionLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			characterView->collisionLayer = (uint16)CollisionLayer::Moving;
		ImGui::EndPopup();
	}

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
	if (ImGui::DragFloat("Mass", &mass, 0.01f, 0.0f, 0.0f, "%.3f kg"))
		characterView->setMass(mass);
	if (ImGui::BeginPopupContextItem("mass"))
	{
		if (ImGui::MenuItem("Reset Default"))
			characterView->setMass(70.0f);
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	renderShapeProperties(characterView, cached);

	if (ImGui::CollapsingHeader("Advanced Properties"))
		renderAdvancedProperties(characterView);
}
#endif
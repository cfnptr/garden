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

#include "garden/editor/system/physics.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/physics-renderer.hpp"
#include "garden/system/render/deferred.hpp"
#include "Jolt/Physics/PhysicsSystem.h"
#include "garden/system/transform.hpp"
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
	ECSM_SUBSCRIBE_TO_EVENT("PreDepthLdrRender", PhysicsEditorSystem::preDepthLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("DepthLdrRender", PhysicsEditorSystem::depthLdrRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", PhysicsEditorSystem::editorBarTool);

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
	delete (PhysicsDebugRenderer*)debugRenderer; // Note: Always destroying!

	if (Manager::Instance::get()->isRunning)
	{
		auto editorSystem = EditorRenderSystem::Instance::get();
		editorSystem->unregisterEntityInspector<RigidbodyComponent>();
		editorSystem->tryUnregisterEntityInspector<CharacterComponent>();

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreDepthLdrRender", PhysicsEditorSystem::preDepthLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("DepthLdrRender", PhysicsEditorSystem::depthLdrRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", PhysicsEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
void PhysicsEditorSystem::preDepthLdrRender()
{
	auto physicsSystem = PhysicsSystem::Instance::get();
	if (!showWindow)
		return;

	if (ImGui::Begin("Physics Simulation", &showWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		int collisionSteps = physicsSystem->collisionSteps;
		if (ImGui::DragInt("Collision Steps", &collisionSteps))
			physicsSystem->collisionSteps = clamp(collisionSteps, 1, 1000);
		int simulationRate = physicsSystem->simulationRate;
		if (ImGui::DragInt("Simulation Rate", &simulationRate))
			physicsSystem->simulationRate = clamp(simulationRate, 1, (int)UINT16_MAX);
		ImGui::SliderFloat("Cascade Lag Threshold", &physicsSystem->cascadeLagThreshold, 0.0f, 1.0f);
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Visualization"))
		{
			ImGui::SeparatorText("Bodies");
			ImGui::Checkbox("Shapes", &drawShapes); ImGui::SameLine();
			ImGui::Checkbox("Bounding Box", &drawBoundingBox); ImGui::SameLine();
			ImGui::Checkbox("Center Of Mass", &drawCenterOfMass);
			ImGui::Spacing();

			ImGui::SeparatorText("Constraints");
			ImGui::Checkbox("Constraints", &drawConstraints); ImGui::SameLine();
			ImGui::Checkbox("Constraint Limits", &drawConstraintLimits); ImGui::SameLine();
			ImGui::Checkbox("Constraint Reference Frame", &drawConstraintRefFrame);
			ImGui::Spacing();
		}

		if (ImGui::CollapsingHeader("Stats Logging"))
		{
			ImGui::Checkbox("Log Broadphase", &physicsSystem->logBroadPhaseStats); ImGui::SameLine();
			ImGui::Checkbox("Log Narrowphase", &physicsSystem->logNarrowPhaseStats);
			ImGui::DragFloat("Stats Log Rate", &physicsSystem->statsLogRate, 1.0f, 0.0f, 0.0f, "%.3f s");
			ImGui::Spacing();
		}

		// TODO: visualize collision matrix and other physics settings.

		if (debugRenderer)
		{
			if (!((PhysicsDebugRenderer*)debugRenderer)->isReady())
				ImGui::TextDisabled("Physics pipelines are loading...");
		}
	}
	ImGui::End();

	if (!drawShapes && !drawConstraints && !drawConstraintLimits && !drawConstraintRefFrame)
		return;

	if (!debugRenderer)
		debugRenderer = new PhysicsDebugRenderer();

	auto renderer = (PhysicsDebugRenderer*)debugRenderer;
	auto instance = (JPH::PhysicsSystem*)PhysicsSystem::Instance::get()->getInstance();
	const auto& cameraConstants = GraphicsSystem::Instance::get()->getCameraConstants();
	renderer->setCameraPosition(cameraConstants.cameraPos);

	JPH::BodyManager::DrawSettings drawSettings;
	drawSettings.mDrawShapeWireframe = true;
	drawSettings.mDrawBoundingBox = drawBoundingBox;
	drawSettings.mDrawCenterOfMassTransform = drawCenterOfMass;

	if (drawShapes)
		instance->DrawBodies(drawSettings, renderer);
	if (drawConstraints)
		instance->DrawConstraints(renderer);
	if (drawConstraintLimits)
		instance->DrawConstraintLimits(renderer);
	if (drawConstraintRefFrame)
		instance->DrawConstraintReferenceFrame(renderer);

	SET_GPU_DEBUG_LABEL("Physics Debug", Color::transparent);
	renderer->preDraw();
}
void PhysicsEditorSystem::depthLdrRender()
{
	if (!drawShapes && !drawConstraints && !drawConstraintLimits && !drawConstraintRefFrame)
		return;

	auto renderer = (PhysicsDebugRenderer*)debugRenderer;
	const auto& cameraConstants = GraphicsSystem::Instance::get()->getCameraConstants();

	SET_GPU_DEBUG_LABEL("Physics Debug", Color::transparent);
	renderer->draw(cameraConstants.viewProj);
}

void PhysicsEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Physics Simulation"))
		showWindow = true;
}

//**********************************************************************************************************************
static void renderEmptyShape(View<RigidbodyComponent> rigidbodyView, 
	PhysicsEditorSystem::RigidbodyCache& cache, bool isChanged)
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
		cache.centerOfMass = shapeView->getBoxConvexRadius();
	isChanged |= ImGui::DragFloat3("Center Of Mass", &cache.centerOfMass, 0.01f);
	if (ImGui::BeginPopupContextItem("centerOfMass"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.centerOfMass = f32x4::zero;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();

		physicsSystem->destroyShared(shape);
		if (cache.shapePosition != f32x4::zero)
		{
			auto innerShape = physicsSystem->createSharedEmptyShape(cache.centerOfMass);
			shape = physicsSystem->createSharedRotTransShape(innerShape, cache.shapePosition);
		}
		else
		{
			shape = physicsSystem->createSharedEmptyShape(cache.centerOfMass);
		}

		rigidbodyView->setShape(shape, cache.motionType, cache.collisionLayer,
			false, rigidbodyView->canBeKinematicOrDynamic(), cache.allowedDOF);
		rigidbodyView->setSensor(cache.isSensor);
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	}
}

//**********************************************************************************************************************
static void renderBoxShape(View<RigidbodyComponent> rigidbodyView, 
	PhysicsEditorSystem::RigidbodyCache& cache, bool isChanged)
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
		cache.halfExtent = shapeView->getBoxHalfExtent();
	isChanged |= ImGui::DragFloat3("Half Extent", &cache.halfExtent, 0.01f);
	if (ImGui::BeginPopupContextItem("halfExtent"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.halfExtent = f32x4(0.5f);
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cache.convexRadius = shapeView->getBoxConvexRadius();
	isChanged |= ImGui::DragFloat("Convex Radius", &cache.convexRadius, 0.01f);
	if (ImGui::BeginPopupContextItem("convexRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.convexRadius = 0.05f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cache.density = shapeView->getDensity();
	isChanged |= ImGui::DragFloat("Density", &cache.density, 1.0f, 0.0f, 0.0f, "%.3f kg/m^3");
	if (ImGui::BeginPopupContextItem("density"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.density = 1000.0f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		cache.convexRadius = max(cache.convexRadius, 0.0f);
		cache.halfExtent = max(cache.halfExtent, f32x4(cache.convexRadius));
		cache.density = max(cache.density, 0.001f);
		auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();

		physicsSystem->destroyShared(shape);
		if (cache.shapePosition != f32x4::zero)
		{
			auto innerShape = physicsSystem->createSharedBoxShape(cache.halfExtent, cache.convexRadius, cache.density);
			shape = physicsSystem->createSharedRotTransShape(innerShape, cache.shapePosition);
		}
		else
		{
			shape = physicsSystem->createSharedBoxShape(cache.halfExtent, cache.convexRadius, cache.density);
		}

		rigidbodyView->setShape(shape, cache.motionType, cache.collisionLayer,
			false, rigidbodyView->canBeKinematicOrDynamic(), cache.allowedDOF);
		rigidbodyView->setSensor(cache.isSensor);
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	}
}

//**********************************************************************************************************************
static void renderSphereShape(View<RigidbodyComponent> rigidbodyView, 
	PhysicsEditorSystem::RigidbodyCache& cache, bool isChanged)
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
		cache.shapeRadius = shapeView->getSphereRadius();
	isChanged |= ImGui::DragFloat("Radius", &cache.shapeRadius, 0.01f);
	if (ImGui::BeginPopupContextItem("shapeRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.shapeRadius = 0.3f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cache.density = shapeView->getDensity();
	isChanged |= ImGui::DragFloat("Density", &cache.density, 1.0f, 0.0f, 0.0f, "%.3f kg/m^3");
	if (ImGui::BeginPopupContextItem("density"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.density = 1000.0f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		cache.shapeRadius = max(cache.shapeRadius, 0.001f);
		cache.density = max(cache.density, 0.001f);
		auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();

		physicsSystem->destroyShared(shape);
		if (cache.shapePosition != f32x4::zero)
		{
			auto innerShape = physicsSystem->createSharedSphereShape(cache.shapeRadius, cache.density);
			shape = physicsSystem->createSharedRotTransShape(innerShape, cache.shapePosition);
		}
		else
		{
			shape = physicsSystem->createSharedSphereShape(cache.shapeRadius, cache.density);
		}

		rigidbodyView->setShape(shape, cache.motionType, cache.collisionLayer,
			false, rigidbodyView->canBeKinematicOrDynamic(), cache.allowedDOF);
		rigidbodyView->setSensor(cache.isSensor);
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	}
}

//**********************************************************************************************************************
static void renderCapsuleShape(View<RigidbodyComponent> rigidbodyView, 
	PhysicsEditorSystem::RigidbodyCache& cache, bool isChanged)
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
		cache.halfExtent = shapeView->getCapsuleHalfHeight();
	isChanged |= ImGui::DragFloat("Half Height", &cache.halfHeight, 0.01f);
	if (ImGui::BeginPopupContextItem("halfHeight"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.halfExtent = 0.875f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cache.shapeRadius = shapeView->getCapsuleRadius();
	isChanged |= ImGui::DragFloat("Radius", &cache.shapeRadius, 0.01f);
	if (ImGui::BeginPopupContextItem("shapeRadius"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.shapeRadius = 0.3f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shape)
		cache.density = shapeView->getDensity();
	isChanged |= ImGui::DragFloat("Density", &cache.density, 1.0f, 0.0f, 0.0f, "%.3f kg/m^3");
	if (ImGui::BeginPopupContextItem("density"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.density = 1000.0f;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (isChanged)
	{
		cache.halfHeight = max(cache.halfHeight, 0.001f);
		cache.shapeRadius = max(cache.shapeRadius, 0.001f);
		cache.density = max(cache.density, 0.001f);
		auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();

		physicsSystem->destroyShared(shape);
		if (cache.shapePosition != f32x4::zero)
		{
			auto innerShape = physicsSystem->createSharedCapsuleShape(cache.halfHeight, cache.shapeRadius, cache.density);
			shape = physicsSystem->createSharedRotTransShape(innerShape, cache.shapePosition);
		}
		else
		{
			shape = physicsSystem->createSharedCapsuleShape(cache.halfHeight, cache.shapeRadius, cache.density);
		}

		rigidbodyView->setShape(shape, cache.motionType, cache.collisionLayer,
			false, rigidbodyView->canBeKinematicOrDynamic(), cache.allowedDOF);
		rigidbodyView->setSensor(cache.isSensor);
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	}
}

//**********************************************************************************************************************
static void renderShapeProperties(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::RigidbodyCache& cache)
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
			cache.shapePosition = shapeView->getPosition();

		if (shapeView->getType() == ShapeType::Decorated)
		{
			innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
		}

		switch (shapeView->getSubType())
		{
		case ShapeSubType::Empty:
			shapeType = 1;
			break;
		case ShapeSubType::Box:
			shapeType = 2;
			break;
		case ShapeSubType::Sphere:
			shapeType = 3;
			break;
		case ShapeSubType::Capsule:
			shapeType = 4;
			break;
		default:
			shapeType = 5;
			break;
		}
	}

	ImGui::BeginDisabled(shapeType == 5);
	isChanged |= ImGui::DragFloat3("Position", &cache.shapePosition, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.shapePosition = f32x4::zero;
			isChanged = true;
		}
		ImGui::EndPopup();
	}
	// TODO: shape rotation
	ImGui::EndDisabled();

	ImGui::PopID();

	constexpr auto sTypes = "None\0Empty\0Box\0Sphere\0Capsule\0Custom\00";
	if (ImGui::Combo("Type", &shapeType, sTypes) || isChanged)
	{
		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			rigidbodyView->setShape({});
			break;
		case 1: case 2: case 3: case 4:
			isChanged = true;
			break;
		default:
			break;
		}
	}

	switch (shapeType)
	{
	case 1:
		renderEmptyShape(rigidbodyView, cache, isChanged);
		break;
	case 2:
		renderBoxShape(rigidbodyView, cache, isChanged);
		break;
	case 3:
		renderSphereShape(rigidbodyView, cache, isChanged);
		break;
	case 4:
		renderCapsuleShape(rigidbodyView, cache, isChanged);
		break;
	default:
		break;
	}
}

//**********************************************************************************************************************
static void renderConstraints(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::RigidbodyCache& cache)
{
	ImGui::Indent();
	ImGui::PushID("constraints");

	ImGui::BeginDisabled(!rigidbodyView->getShape());
	constexpr const char* constraintNames[(uint8)ConstraintType::Count] = { "Fixed", "Point" };
	ImGui::Combo("Type", cache.constraintType, constraintNames, (int)ConstraintType::Count);

	auto name = cache.constraintTarget ? "Entity " + to_string(*cache.constraintTarget) : "";
	if (cache.constraintTarget)
	{
		auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(cache.constraintTarget);
		if (transformView && !transformView->debugName.empty())
			name = transformView->debugName;
	}

	ImGui::InputText("Other Entity", &name, ImGuiInputTextFlags_ReadOnly);
	if (ImGui::BeginPopupContextItem("otherEntity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cache.constraintTarget = {};
		if (ImGui::MenuItem("Select Entity"))
			EditorRenderSystem::Instance::get()->selectedEntity = cache.constraintTarget;
		ImGui::EndPopup();
	}
	if (ImGui::BeginDragDropTarget())
	{
		auto payload = ImGui::AcceptDragDropPayload("Entity");
		if (payload)
		{
			GARDEN_ASSERT(payload->DataSize == sizeof(ID<Entity>));
			cache.constraintTarget = *((const ID<Entity>*)payload->Data);
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::Checkbox("Auto Points", &cache.autoConstraintPoints);

	ImGui::BeginDisabled(cache.autoConstraintPoints);
	ImGui::DragFloat3("This Point", &cache.thisConstraintPoint, 0.01f);
	if (ImGui::BeginPopupContextItem("thisPoint"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cache.thisConstraintPoint = f32x4::zero;
		ImGui::EndPopup();
	}
	ImGui::DragFloat3("Other Point", &cache.otherConstraintPoint, 0.01f);
	if (ImGui::BeginPopupContextItem("otherPoint"))
	{
		if (ImGui::MenuItem("Reset Default"))
			cache.otherConstraintPoint = f32x4::zero;
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	auto canCreate = !cache.constraintTarget;
	if (cache.constraintTarget && cache.constraintTarget != rigidbodyView->getEntity())
	{
		auto otherView = PhysicsSystem::Instance::get()->tryGetComponent(cache.constraintTarget);
		if (otherView && otherView->getShape())
			canCreate = true;
	}

	ImGui::Separator();

	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create Constraint", ImVec2(-FLT_MIN, 0.0f)))
	{
		rigidbodyView->createConstraint(cache.constraintTarget, cache.constraintType,
			cache.autoConstraintPoints ? f32x4::max : cache.thisConstraintPoint, 
			cache.autoConstraintPoints ? f32x4::max : cache.otherConstraintPoint);
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
			ImGui::Combo("Type", type, constraintNames, (int)ConstraintType::Count);

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
					EditorRenderSystem::Instance::get()->selectedEntity = cache.constraintTarget;
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
static void renderAdvancedProperties(View<RigidbodyComponent> rigidbodyView, PhysicsEditorSystem::RigidbodyCache& cache)
{
	ImGui::Indent();
	
	auto shape = rigidbodyView->getShape();
	auto isAnyChanged = false;

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() != MotionType::Kinematic);
	auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();
	if (ImGui::Checkbox("Kinematic VS Static", &isKinematicVsStatic))
		rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
	ImGui::EndDisabled();

	ImGui::Spacing();

	ImGui::BeginDisabled(rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Degrees of Freedom (DOF)");

	if (shape)
		cache.allowedDOF = rigidbodyView->getAllowedDOF();

	ImGui::SameLine();
	if (ImGui::SmallButton("Set 3D"))
	{
		cache.allowedDOF = AllowedDOF::All;
		isAnyChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Set 2D"))
	{
		cache.allowedDOF = AllowedDOF::TranslationX | 
			AllowedDOF::TranslationY | AllowedDOF::RotationZ;
		isAnyChanged = true;
	}

	ImGui::Text("Translation:"); ImGui::SameLine();
	ImGui::PushID("dofTranslation");
	auto transX = hasAnyFlag(cache.allowedDOF, AllowedDOF::TranslationX);
	isAnyChanged |= ImGui::Checkbox("X", &transX); ImGui::SameLine();
	auto transY = hasAnyFlag(cache.allowedDOF, AllowedDOF::TranslationY);
	isAnyChanged |= ImGui::Checkbox("Y", &transY); ImGui::SameLine();
	auto transZ = hasAnyFlag(cache.allowedDOF, AllowedDOF::TranslationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &transZ);
	ImGui::PopID();

	ImGui::Text("Rotation:   "); ImGui::SameLine();
	ImGui::PushID("dofRotation");
	auto rotX = hasAnyFlag(cache.allowedDOF, AllowedDOF::RotationX);
	isAnyChanged |= ImGui::Checkbox("X", &rotX); ImGui::SameLine();
	auto rotY = hasAnyFlag(cache.allowedDOF, AllowedDOF::RotationY);
	isAnyChanged |= ImGui::Checkbox("Y", &rotY); ImGui::SameLine();
	auto rotZ = hasAnyFlag(cache.allowedDOF, AllowedDOF::RotationZ);
	isAnyChanged |= ImGui::Checkbox("Z", &rotZ);
	ImGui::PopID();
	ImGui::EndDisabled();

	if (isAnyChanged)
	{
		cache.allowedDOF = AllowedDOF::None;
		if (transX) cache.allowedDOF |= AllowedDOF::TranslationX;
		if (transY) cache.allowedDOF |= AllowedDOF::TranslationY;
		if (transZ) cache.allowedDOF |= AllowedDOF::TranslationZ;
		if (rotX) cache.allowedDOF |= AllowedDOF::RotationX;
		if (rotY) cache.allowedDOF |= AllowedDOF::RotationY;
		if (rotZ) cache.allowedDOF |= AllowedDOF::RotationZ;

		if (shape)
		{
			auto motionType = rigidbodyView->getMotionType();
			auto collisionLayer = rigidbodyView->getCollisionLayer();
			auto canBeKinematicOrDynamic = rigidbodyView->canBeKinematicOrDynamic();
			auto isSensor = rigidbodyView->isSensor();
			auto isKinematicVsStatic = rigidbodyView->isKinematicVsStatic();

			if (cache.allowedDOF == AllowedDOF::None && motionType != MotionType::Static)
				cache.motionType = motionType = MotionType::Static;
			
			rigidbodyView->setShape({});
			rigidbodyView->setShape(shape, motionType, collisionLayer, 
				false, canBeKinematicOrDynamic, cache.allowedDOF);
			rigidbodyView->setSensor(isSensor);
			rigidbodyView->setKinematicVsStatic(isKinematicVsStatic);
		}
	}

	ImGui::Spacing();

	ImGui::BeginDisabled(!shape || rigidbodyView->getMotionType() == MotionType::Static);
	ImGui::SeparatorText("Velocity");

	auto velocity = rigidbodyView->getLinearVelocity();
	if (ImGui::DragFloat3("Linear", &velocity, 0.01f, 0.0f, 0.0f, "%.3f m/s"))
		rigidbodyView->setLinearVelocity(velocity);
	if (ImGui::BeginPopupContextItem("linearVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setLinearVelocity(f32x4::zero);
		ImGui::EndPopup();
	}

	velocity = rigidbodyView->getAngularVelocity();
	if (ImGui::DragFloat3("Angular", &velocity, 0.01f, 0.0f, 0.0f, "%.3f rad/s"))
		rigidbodyView->setAngularVelocity(velocity);
	if (ImGui::BeginPopupContextItem("angularVelocity"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setAngularVelocity(f32x4::zero);
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	ImGui::Spacing();

	ImGui::SeparatorText("Shape Properties");
	ImGui::BeginDisabled();
	
	View<Shape> shapeView;
	if (shape)
		shapeView = PhysicsSystem::Instance::get()->get(shape);

	auto centerOfMass = f32x4::zero;
	if (shape)
		centerOfMass = shapeView->getCenterOfMass();
	ImGui::DragFloat3("Center Of Mass", &centerOfMass);

	auto volume = 0.0f;
	if (shape)
		volume = shapeView->getVolume();
	ImGui::DragFloat("Volume", &volume, 1.0f, 0.0f, 0.0f, "%.3f m^3");

	auto mass = 0.0f; auto inertia = f32x4x4::zero;
	if (shape)
		shapeView->getMassProperties(mass, inertia);
	ImGui::DragFloat("Mass", &mass, 1.0f, 0.0f, 0.0f, "%.3f kg");
	ImGui::TextWrapped("Inertia Tensor (kg/m^2):\n"
		"%f, %f, %f, %f\n%f, %f, %f, %f\n%f, %f, %f, %f\n%f, %f, %f, %f", 
		inertia.c0.getX(), inertia.c1.getY(), inertia.c2.getZ(), inertia.c3.getW(),
		inertia.c0.getX(), inertia.c1.getY(), inertia.c2.getZ(), inertia.c3.getW(),
		inertia.c0.getX(), inertia.c1.getY(), inertia.c2.getZ(), inertia.c3.getW(),
		inertia.c0.getX(), inertia.c1.getY(), inertia.c2.getZ(), inertia.c3.getW());
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
				oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.extractEulerAngles());
				oldRigidbodyRotation = rotation;
			}
		}

		rigidbodySelectedEntity = entity;
		rigidbodyCache = {};
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
					oldRigidbodyEulerAngles = newRigidbodyEulerAngles = degrees(rotation.extractEulerAngles());
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

	ImGui::BeginDisabled(!shape);
	if (shape)
		rigidbodyCache.isSensor = rigidbodyView->isSensor();
	ImGui::SameLine();
	if (ImGui::Checkbox("Sensor", &rigidbodyCache.isSensor))
		rigidbodyView->setSensor(rigidbodyCache.isSensor);
	ImGui::EndDisabled();

	auto isInSimulation = rigidbodyView->isInSimulation();
	ImGui::SameLine();
	if (ImGui::Checkbox("In Simulation", &isInSimulation))
		rigidbodyView->setInSimulation(isInSimulation);

	ImGui::BeginDisabled(!rigidbodyCache.isSensor);
	ImGui::InputText("Event Listener", &rigidbodyView->eventListener);
	if (ImGui::BeginPopupContextItem("eventListener"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->eventListener = "";
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!shape);
	f32x4 position; quat rotation;
	rigidbodyView->getPosAndRot(position, rotation);
	if (ImGui::DragFloat3("Position", &position, 0.01f))
		rigidbodyView->setPosition(position, false);
	if (ImGui::BeginPopupContextItem("position"))
	{
		if (ImGui::MenuItem("Reset Default"))
			rigidbodyView->setPosition(f32x4::zero, false);
		ImGui::EndPopup();
	}

	if (ImGui::DragFloat3("Rotation", &newRigidbodyEulerAngles, 0.3f, 0.0f, 0.0f, "%.3f°"))
	{
		auto difference = newRigidbodyEulerAngles - oldRigidbodyEulerAngles;
		rotation *= fromEulerAngles(radians(difference));
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
		ImGui::Text("Rotation in degrees.\nRadians: %.3f, %.3f, %.3f",
			rotation.getX(), rotation.getY(), rotation.getZ());
		ImGui::EndTooltip();
	}

	if (shape)
		rigidbodyCache.collisionLayer = (int)rigidbodyView->getCollisionLayer();
	if (ImGui::DragInt("Collision Layer", &rigidbodyCache.collisionLayer))
		rigidbodyView->setCollisionLayer(rigidbodyCache.collisionLayer);
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
	if (shape)
		rigidbodyCache.motionType = rigidbodyView->getMotionType();
	constexpr const char* motionNames[(uint8)MotionType::Count] = { "Static", "Kinematic", "Dynamic" };
	if (ImGui::Combo("Motion Type", rigidbodyCache.motionType, motionNames, (int)MotionType::Count))
	{
		if (rigidbodyCache.motionType != MotionType::Static)
			rigidbodyCache.allowedDOF = AllowedDOF::All;
		rigidbodyCache.collisionLayer = -1;
		rigidbodyView->setMotionType(rigidbodyCache.motionType);
	}
	ImGui::EndDisabled();

	renderShapeProperties(rigidbodyView, rigidbodyCache);
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Constraints"))
		renderConstraints(rigidbodyView, rigidbodyCache);
	if (ImGui::CollapsingHeader("Advanced Properties"))
		renderAdvancedProperties(rigidbodyView, rigidbodyCache);
}

//**********************************************************************************************************************
static void renderShapeProperties(View<CharacterComponent> characterView, PhysicsEditorSystem::CharacterCache& cache)
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
				cache.shapePosition = shapeView->getPosition();

			auto innerShape = shapeView->getInnerShape();
			shapeView = physicsSystem->get(innerShape);
			switch (shapeView->getSubType())
			{
			case ShapeSubType::Empty:
				cache.centerOfMass = shapeView->getCenterOfMass();
				shapeType = 1;
				break;
			case ShapeSubType::Box:
				cache.shapeSize = shapeView->getBoxHalfExtent() * 2.0f;
				cache.convexRadius = shapeView->getBoxConvexRadius();
				shapeType = 2;
				break;
			case ShapeSubType::Capsule:
				cache.shapeHeight = shapeView->getCapsuleHalfHeight() * 2.0f;
				cache.shapeRadius = shapeView->getCapsuleRadius();
				shapeType = 3;
				break;
			default:
				shapeType = 4;
				break;
			}
		}
		else
		{
			shapeType = 4;
		}
	}

	isChanged |= ImGui::DragFloat3("Position", &cache.shapePosition, 0.01f);
	if (ImGui::BeginPopupContextItem("shapePosition"))
	{
		if (ImGui::MenuItem("Reset Default"))
		{
			cache.shapePosition = f32x4::zero;
			isChanged = true;
		}
		ImGui::EndPopup();
	}

	if (shapeType == 2)
	{
		isChanged |= ImGui::DragFloat3("Size", &cache.shapeSize, 0.01f);
		if (ImGui::BeginPopupContextItem("shapeSize"))
		{
			if (ImGui::MenuItem("Reset Default"))
			{
				cache.shapeSize = f32x4(0.5f, 1.75f, 0.5f);
				isChanged = true;
			}
			ImGui::EndPopup();
		}

		isChanged |= ImGui::DragFloat("Convex Radius", &cache.convexRadius, 0.01f);
		if (ImGui::BeginPopupContextItem("convexRadius"))
		{
			if (ImGui::MenuItem("Reset Default"))
			{
				cache.convexRadius = 0.05f;
				isChanged = true;
			}
			ImGui::EndPopup();
		}
	}
	else if (shapeType == 3)
	{
		isChanged |= ImGui::DragFloat("Height", &cache.shapeHeight, 0.01f);
		if (ImGui::BeginPopupContextItem("shapeHeight"))
		{
			if (ImGui::MenuItem("Reset Default"))
			{
				cache.shapeHeight = 1.75f;
				isChanged = true;
			}
			ImGui::EndPopup();
		}

		isChanged |= ImGui::DragFloat("Radius", &cache.shapeRadius, 0.01f);
		if (ImGui::BeginPopupContextItem("shapeRadius"))
		{
			if (ImGui::MenuItem("Reset Default"))
			{
				cache.shapeRadius = 0.3f;
				isChanged = true;
			}
			ImGui::EndPopup();
		}
	}

	// TODO: shape rotation

	constexpr const char* shapeNames[5] = { "None", "Empty", "Box", "Capsule", "Custom" };
	if ((ImGui::Combo("Type", &shapeType, shapeNames, 5) || isChanged))
	{
		cache.convexRadius = max(cache.convexRadius, 0.0f);
		cache.shapeSize = max(cache.shapeSize, f32x4(cache.convexRadius * 2.0f));
		ID<Shape> innerShape = {};

		switch (shapeType)
		{
		case 0:
			physicsSystem->destroyShared(shape);
			characterView->setShape({});
			break;
		case 1:
			physicsSystem->destroyShared(shape);
			innerShape = physicsSystem->createSharedEmptyShape(cache.centerOfMass);
			shape = physicsSystem->createRotTransShape(innerShape, cache.shapePosition);
			characterView->setShape(shape);
			break;
		case 2:
			physicsSystem->destroyShared(shape);
			innerShape = physicsSystem->createSharedEmptyShape(cache.centerOfMass);
			shape = physicsSystem->createRotTransShape(innerShape, cache.shapePosition);
			characterView->setShape(shape);
			break;
		case 3:
			physicsSystem->destroyShared(shape);
			innerShape = physicsSystem->createSharedCapsuleShape(cache.shapeHeight * 0.5f, cache.shapeRadius);
			shape = physicsSystem->createRotTransShape(innerShape, cache.shapePosition);
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
			characterView->setLinearVelocity(f32x4::zero);
		ImGui::EndPopup();
	}

	string stateString = "";
	if (characterView->getShape())
	{
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
	}
	
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
				oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.extractEulerAngles());
				oldCharacterRotation = rotation;
			}
		}

		characterSelectedEntity = entity;
		characterCache = {};
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
					oldCharacterEulerAngles = newCharacterEulerAngles = degrees(rotation.extractEulerAngles());
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

	f32x4 position; quat rotation;
	characterView->getPosAndRot(position, rotation);
	if (ImGui::DragFloat3("Position", &position, 0.01f))
		characterView->setPosition(position);
	if (ImGui::BeginPopupContextItem("position"))
	{
		if (ImGui::MenuItem("Reset Default"))
			characterView->setPosition(f32x4::zero);
		ImGui::EndPopup();
	}

	if (ImGui::DragFloat3("Rotation", &newCharacterEulerAngles, 0.3f, 0.0f, 0.0f, "%.3f°"))
	{
		auto difference = newCharacterEulerAngles - oldCharacterEulerAngles;
		rotation *= fromEulerAngles(radians(difference));
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
		ImGui::Text("Rotation in degrees.\nRadians: %.3f, %.3f, %.3f",
			rotation.getX(), rotation.getY(), rotation.getZ());
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

	renderShapeProperties(characterView, characterCache);

	if (ImGui::CollapsingHeader("Advanced Properties"))
		renderAdvancedProperties(characterView);
}
#endif
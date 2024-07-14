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
	GARDEN_ASSERT(Manager::getInstance()->has<EditorRenderSystem>());
	EditorRenderSystem::getInstance()->registerEntityInspector<RigidbodyComponent>(
	[this](ID<Entity> entity, bool isOpened)
	{
		onEntityInspector(entity, isOpened);
	});
}
void PhysicsEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->unregisterEntityInspector<RigidbodyComponent>();
}

//**********************************************************************************************************************
void PhysicsEditorSystem::onEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto rigidbodyComponent = Manager::getInstance()->get<RigidbodyComponent>(entity);
		ImGui::Text("Active: %s", rigidbodyComponent->isActive() ? "true" : "false");
		ImGui::EndTooltip();
	}

	if (!isOpened)
		return;

	auto rigidbodyComponent = Manager::getInstance()->get<RigidbodyComponent>(entity);

	auto isActive = rigidbodyComponent->isActive();
	if (ImGui::Checkbox("Active", &isActive))
	{
		if (isActive && rigidbodyComponent->getShape())
			rigidbodyComponent->activate();
		else
			rigidbodyComponent->deactivate();
	}

	const auto mTypes = "Static\0Kinematic\0Dynamic\00";
	auto motionType = rigidbodyComponent->getMotionType();
	if (ImGui::Combo("Motion Type", &motionType, mTypes))
		rigidbodyComponent->setMotionType(motionType);
	

	auto physicsSystem = PhysicsSystem::getInstance();
	auto shape = rigidbodyComponent->getShape();

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
			rigidbodyComponent->setShape({});
			break;
		case 2:
			rigidbodyComponent->setShape(physicsSystem->createBoxShape(float3(0.5f)));
			break;
		default:
			break;
		}
	}
}
#endif
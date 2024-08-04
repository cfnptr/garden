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

#include "platformer.hpp"
#include "garden/system/link.hpp"
#include "garden/system/physics.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"

using namespace platformer;

//**********************************************************************************************************************
PlatformerSystem::PlatformerSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", PlatformerSystem::init);
}
PlatformerSystem::~PlatformerSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", PlatformerSystem::init);
	}
}

//**********************************************************************************************************************
void PlatformerSystem::init()
{
	ResourceSystem::getInstance()->loadScene("platformer");

	auto linkSystem = LinkSystem::getInstance();
	auto physicsSystem = PhysicsSystem::getInstance();

	auto items = linkSystem->findEntities("Item");
	for (auto i = items.first; i != items.second; i++)
	{
		auto linkView = linkSystem->get(i->second);
		auto rigidbodyView = physicsSystem->tryGet(linkView->getEntity());
		if (rigidbodyView)
		{
			rigidbodyView->addListener<PlatformerSystem>([this](ID<Entity> thisEntity, ID<Entity> otherEntity)
			{
				onItemSensor(thisEntity, otherEntity);
			},
			BodyEvent::ContactAdded);
		}
	}
}

//**********************************************************************************************************************
void PlatformerSystem::onItemSensor(ID<Entity> thisEntity, ID<Entity> otherEntity)
{
	auto transformSystem = TransformSystem::getInstance();
	auto transformView = transformSystem->tryGet(thisEntity);
	if (transformView)
	{
		if (transformView->getParent())
			transformSystem->destroyRecursive(transformView->getParent());
		else
			transformSystem->destroyRecursive(thisEntity);
	}

	// TODO: spawn particles.
}
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

#include "garden/system/transform.hpp"
#include <memory>

using namespace garden;

static void testComponents()
{
	auto manager = make_shared<Manager>();
	manager->createSystem<TransformSystem>();
	manager->initialize();
	
	vector<ID<Entity>> entities(16);
	for (uint32 i = 0; i < 16; i++)
	{
		auto entity = manager->createEntity();
		auto transformView = manager->add<TransformComponent>(entity);
		transformView->position.x = i;
		transformView->scale.y = i;
		entities[i] = entity;
	}

	if (manager->getEntities().getOccupancy() != 16)
		throw runtime_error("Incorrect manager entity count.");

	auto transformSystem = manager->get<TransformSystem>();
	
	if (transformSystem->getComponents().getOccupancy() != 16)
		throw runtime_error("Incorrect system component count.");

	auto transformView = TransformSystem::Instance::get()->getComponent(entities[6]);
	if (transformView->position.x != 6 || transformView->scale.y != 6)
		throw runtime_error("Bad transform component data.");

	manager->destroySystem<TransformSystem>();
}

// TODO: test inheritance parent/childs, setActive.

int main()
{
	testComponents();
}
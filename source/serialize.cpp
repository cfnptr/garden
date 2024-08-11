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

#include "garden/serialize.hpp"
#include "garden/system/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
DoNotSerializeSystem* DoNotSerializeSystem::instance = nullptr;

DoNotSerializeSystem::DoNotSerializeSystem()
{
	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
DoNotSerializeSystem::~DoNotSerializeSystem()
{
	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

ID<Component> DoNotSerializeSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void DoNotSerializeSystem::destroyComponent(ID<Component> instance)
{ 
	components.destroy(ID<DoNotSerializeComponent>(instance));
}
void DoNotSerializeSystem::copyComponent(View<Component> source, View<Component> destination)
{
	return;
}

const string& DoNotSerializeSystem::getComponentName() const
{
	static const string name = "Do Not Serialize";
	return name;
}
type_index DoNotSerializeSystem::getComponentType() const
{
	return typeid(DoNotSerializeComponent);
}
View<Component> DoNotSerializeSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<DoNotSerializeComponent>(instance)));
}
void DoNotSerializeSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
bool DoNotSerializeSystem::hasOrAncestors(ID<Entity> entity) const
{
	if (has(entity))
		return true;

	auto transformSystem = TransformSystem::get();
	auto transformView = transformSystem->tryGet(entity);
	if (!transformView)
		return false;

	auto parent = transformView->getParent();
	while (parent)
	{
		if (has(parent))
			return true;
		transformView = transformSystem->get(parent);
		parent = transformView->getParent();
	}
	return false;
}
bool DoNotSerializeSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(DoNotSerializeComponent)) != entityComponents.end();
}
View<DoNotSerializeComponent> DoNotSerializeSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(DoNotSerializeComponent));
	return components.get(ID<DoNotSerializeComponent>(pair.second));
}
View<DoNotSerializeComponent> DoNotSerializeSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(DoNotSerializeComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<DoNotSerializeComponent>(result->second.second));
}
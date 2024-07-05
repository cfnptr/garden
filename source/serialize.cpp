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

using namespace garden;

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
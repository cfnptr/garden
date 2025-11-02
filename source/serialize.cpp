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

#include "garden/serialize.hpp"
#include "garden/system/transform.hpp"

using namespace garden;

DoNotSerializeSystem::DoNotSerializeSystem(bool setSingleton) : Singleton(setSingleton) { }
DoNotSerializeSystem::~DoNotSerializeSystem() { unsetSingleton(); }

string_view DoNotSerializeSystem::getComponentName() const
{
	return "Do Not Serialize";
}

bool DoNotSerializeSystem::hasOrAncestors(ID<Entity> entity) const
{
	auto manager = Manager::Instance::get();
	if (manager->has<DoNotSerializeComponent>(entity))
		return true;

	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView)
		return false;

	auto parent = transformView->getParent();
	while (parent)
	{
		if (manager->has<DoNotSerializeComponent>(parent))
			return true;
		auto parentView = manager->get<TransformComponent>(parent);
		parent = parentView->getParent();
	}
	return false;
}
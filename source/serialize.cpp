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

//**********************************************************************************************************************
DoNotSerializeSystem::DoNotSerializeSystem(bool setSingleton) : Singleton(setSingleton) { }
DoNotSerializeSystem::~DoNotSerializeSystem() { unsetSingleton(); }

const string& DoNotSerializeSystem::getComponentName() const
{
	static const string name = "Do Not Serialize";
	return name;
}

//**********************************************************************************************************************
bool DoNotSerializeSystem::hasOrAncestors(ID<Entity> entity) const
{
	if (hasComponent(entity))
		return true;

	auto transformSystem = TransformSystem::Instance::get();
	auto transformView = transformSystem->tryGetComponent(entity);
	if (!transformView)
		return false;

	auto parent = transformView->getParent();
	while (parent)
	{
		if (hasComponent(parent))
			return true;
		transformView = transformSystem->getComponent(parent);
		parent = transformView->getParent();
	}
	return false;
}
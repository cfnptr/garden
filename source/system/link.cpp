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

#include "garden/system/link.hpp"
#include "garden/system/input.hpp"
#include "garden/system/log.hpp"

using namespace garden;

//**********************************************************************************************************************
bool LinkComponent::destroy()
{
	if (uuid == Hash128())
		return true;

	auto linkSystem = LinkSystem::getInstance();
	auto result = linkSystem->linkMap.erase(uuid);
	GARDEN_ASSERT(result == 1); // Failed to remove link, corruped memory.
	return true;
}

LinkSystem* LinkSystem::instance = nullptr;

LinkSystem::LinkSystem()
{
	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
LinkSystem::~LinkSystem()
{
	components.clear(false);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
ID<Component> LinkSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void LinkSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<LinkComponent>(instance));
}
void LinkSystem::copyComponent(View<Component> source, View<Component> destination)
{
	return; // Copied component will have a new UUID.
}
const string& LinkSystem::getComponentName() const
{
	static const string name = "Link";
	return name;
}
type_index LinkSystem::getComponentType() const
{
	return typeid(LinkComponent);
}
View<Component> LinkSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<LinkComponent>(instance)));
}
void LinkSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
void LinkSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<LinkComponent>(component);
	auto uuid = componentView->uuid.toBase64();
	serializer.write("uuid", uuid);
}
void LinkSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<LinkComponent>(component);

	string uuid;
	if (deserializer.read("uuid", uuid))
	{
		if (!componentView->uuid.fromBase64(uuid))
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Deserialized entity with invalid link uuid. (uuid: " + uuid + ")");
		}
	}
	else
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Deserialized entity with empty link uuid.");
	}
}

ID<Entity> LinkSystem::findEntity(const Hash128& uuid)
{
	auto searchResult = linkMap.find(uuid);
	if (searchResult == linkMap.end())
		return {};
	auto componentView = components.get(searchResult->second);
	return componentView->getEntity();
}
void LinkSystem::regenerateUUID(View<LinkComponent> component)
{
	GARDEN_ASSERT(component);
	linkMap.erase(component->uuid);
	auto time = InputSystem::getInstance()->getTime();
	component->uuid = Hash128::generateRandom(*(uint64*)(&time));
	auto emplaceResult = linkMap.emplace(component->uuid, component->getEntity());
	GARDEN_ASSERT(emplaceResult.second); // Whoops random hash collision occured. You are lucky :)
	if (!emplaceResult.second) component->uuid = {};
}
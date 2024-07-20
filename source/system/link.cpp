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
#include "garden/system/log.hpp"

using namespace garden;

static void eraseEntityTag(LinearPool<LinkComponent>& components,
	multimap<string, ID<LinkComponent>>& tagMap, LinkComponent* component, const string& tag)
{
	auto range = tagMap.equal_range(tag);
	auto componentID = components.getID(component);
	for (auto i = range.first; i != range.second; i++)
	{
		if (i->second != componentID)
			continue;
		tagMap.erase(i);
		break;
	}
}

//**********************************************************************************************************************
bool LinkComponent::destroy()
{
	auto linkSystem = LinkSystem::getInstance();
	if (uuid)
	{
		auto result = linkSystem->uuidMap.erase(uuid);
		GARDEN_ASSERT(result == 1); // Failed to remove link, corruped memory.
	}
	if (!tag.empty())
		eraseEntityTag(linkSystem->components, linkSystem->tagMap, this, tag);
	return true;
}

void LinkComponent::regenerateUUID()
{
	auto linkSystem = LinkSystem::getInstance();
	if (uuid)
	{
		auto result = linkSystem->uuidMap.erase(uuid);
		GARDEN_ASSERT(result == 1); // Failed to remove link, corrupted memory.
	}

	auto& randomDevice = linkSystem->randomDevice;
	uint32 seed[2] { randomDevice(), randomDevice() };
	uuid = Hash128::generateRandom(*(uint64*)(seed));
	auto componentID = linkSystem->components.getID(this);
	auto result = linkSystem->uuidMap.emplace(uuid, componentID);
	GARDEN_ASSERT(result.second); // Whoops random hash collision occured. You are lucky :)
	if (!result.second) uuid = {};
}
bool LinkComponent::trySetUUID(const Hash128& uuid)
{
	if (this->uuid == uuid)
		return true;

	if (uuid)
	{
		auto linkSystem = LinkSystem::getInstance();
		auto componentID = linkSystem->components.getID(this);
		auto result = linkSystem->uuidMap.emplace(uuid, componentID);
		if (!result.second)
			return false;
	}
	if (this->uuid)
	{
		auto linkSystem = LinkSystem::getInstance();
		auto result = linkSystem->uuidMap.erase(this->uuid);
		GARDEN_ASSERT(result == 1); // Failed to remove link, corrupted memory.
	}

	this->uuid = uuid;
	return true;
}
void LinkComponent::setTag(const string& tag)
{
	if (this->tag == tag)
		return;

	auto linkSystem = LinkSystem::getInstance();
	if (!this->tag.empty())
		eraseEntityTag(linkSystem->components, linkSystem->tagMap, this, this->tag);

	if (!tag.empty())
	{
		auto componentID = linkSystem->components.getID(this);
		linkSystem->tagMap.emplace(tag, componentID);
	}

	this->tag = tag;
}

//**********************************************************************************************************************
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
	const auto sourceView = View<LinkComponent>(source);
	auto destinationView = View<LinkComponent>(destination);

	if (!destinationView->tag.empty())
		eraseEntityTag(components, tagMap, *destinationView, destinationView->tag);

	const auto& tag = sourceView->tag;
	if (tag.empty())
	{
		destinationView->tag = "";
	}
	else
	{
		auto componentID = components.getID(*destinationView);
		tagMap.emplace(tag, componentID);
		destinationView->tag = tag;
	}
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

	string value;
	if (deserializer.read("uuid", value))
	{
		if (!componentView->uuid.fromBase64(value))
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Deserialized entity with invalid link uuid. (uuid: " + value + ")");
		}

		auto componentID = components.getID(*componentView);
		auto result = uuidMap.emplace(componentView->uuid, componentID);

		if (!result.second)
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Deserialized entity with already existing link uuid. (uuid: " + value + ")");
			componentView->uuid = {};
		}
	}
	if (deserializer.read("tag", value))
	{
		auto componentID = components.getID(*componentView);
		tagMap.emplace(value, componentID);
		componentView->tag = std::move(value);
	}
}

//**********************************************************************************************************************
ID<Entity> LinkSystem::findEntity(const Hash128& uuid)
{
	GARDEN_ASSERT(uuid);
	auto searchResult = uuidMap.find(uuid);
	if (searchResult == uuidMap.end())
		return {};
	auto componentView = components.get(searchResult->second);
	return componentView->getEntity();
}
void LinkSystem::findEntities(const string& tag, vector<ID<Entity>>& entities)
{
	GARDEN_ASSERT(!tag.empty());
	auto result = tagMap.equal_range(tag);
	for (auto i = result.first; i != result.second; i++)
	{
		auto componentView = components.get(i->second);
		entities.emplace_back(componentView->entity);
	}
}

bool LinkSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(LinkComponent)) != entityComponents.end();
}
View<LinkComponent> LinkSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(LinkComponent));
	return components.get(ID<LinkComponent>(pair.second));
}
View<LinkComponent> LinkSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(LinkComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<LinkComponent>(result->second.second));
}
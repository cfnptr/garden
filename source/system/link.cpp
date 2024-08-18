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

static void eraseEntityTag(multimap<string, ID<Entity>>& tagMap, ID<Entity> entity, const string& tag)
{
	auto range = tagMap.equal_range(tag);
	for (auto i = range.first; i != range.second; i++)
	{
		if (i->second != entity)
			continue;
		tagMap.erase(i);
		break;
	}
}

//**********************************************************************************************************************
bool LinkComponent::destroy()
{
	auto linkSystem = LinkSystem::get();
	if (uuid)
	{
		auto result = linkSystem->uuidMap.erase(uuid);
		GARDEN_ASSERT(result == 1); // Failed to remove link, corruped memory.
		uuid = {};
	}
	if (!tag.empty())
	{
		eraseEntityTag(linkSystem->tagMap, entity, tag);
		tag = "";
	}
	return true;
}

void LinkComponent::regenerateUUID()
{
	auto linkSystem = LinkSystem::get();
	if (uuid)
	{
		auto result = linkSystem->uuidMap.erase(uuid);
		GARDEN_ASSERT(result == 1); // Failed to remove link, corrupted memory.
	}

	auto& randomDevice = linkSystem->randomDevice;
	uint32 seed[2] { randomDevice(), randomDevice() };
	uuid = Hash128::generateRandom(*(uint64*)(seed));
	auto result = linkSystem->uuidMap.emplace(uuid, entity);
	
	if (!result.second)
		throw runtime_error("Link UUID collision occured.");
}
bool LinkComponent::trySetUUID(const Hash128& uuid)
{
	if (this->uuid == uuid)
		return true;

	if (uuid)
	{
		auto linkSystem = LinkSystem::get();
		auto result = linkSystem->uuidMap.emplace(uuid, entity);
		if (!result.second)
			return false;
	}
	if (this->uuid)
	{
		auto linkSystem = LinkSystem::get();
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

	auto linkSystem = LinkSystem::get();
	if (!this->tag.empty())
		eraseEntityTag(linkSystem->tagMap, entity, this->tag);
	if (!tag.empty())
		linkSystem->tagMap.emplace(tag, entity);

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
	destinationView->destroy();

	if (sourceView->uuid)
		destinationView->regenerateUUID();

	if (!sourceView->tag.empty())
	{
		tagMap.emplace(sourceView->tag, destinationView->entity);
		destinationView->tag = sourceView->tag;
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
void LinkSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<LinkComponent>(component);

	if (componentView->uuid)
	{
		componentView->uuid.toBase64(uuidStringCache);
		serializer.write("uuid", uuidStringCache);
	}

	if (!componentView->tag.empty())
		serializer.write("tag", componentView->tag);
}
void LinkSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<LinkComponent>(component);

	if (deserializer.read("uuid", uuidStringCache))
	{
		if (!componentView->uuid.fromBase64(uuidStringCache))
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Deserialized entity with invalid link uuid. (uuid: " + uuidStringCache + ")");
		}

		auto result = uuidMap.emplace(componentView->uuid, entity);
		if (!result.second)
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
			{
				logSystem->error("Deserialized entity with already "
					"existing link uuid. (uuid: " + uuidStringCache + ")");
			}
			componentView->uuid = {};
		}
	}

	string tag;
	if (deserializer.read("tag", tag))
	{
		tagMap.emplace(tag, entity);
		componentView->tag = std::move(tag);
	}
}

//**********************************************************************************************************************
ID<Entity> LinkSystem::findEntity(const Hash128& uuid) const
{
	GARDEN_ASSERT(uuid);
	auto searchResult = uuidMap.find(uuid);
	if (searchResult == uuidMap.end())
		return {};
	return searchResult->second;
}
void LinkSystem::findEntities(const string& tag, vector<ID<Entity>>& entities) const
{
	GARDEN_ASSERT(!tag.empty());
	auto result = tagMap.equal_range(tag);
	for (auto i = result.first; i != result.second; i++)
		entities.emplace_back(i->second);
}

bool LinkSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(LinkComponent)) != entityComponents.end();
}
View<LinkComponent> LinkSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(LinkComponent));
	return components.get(ID<LinkComponent>(pair.second));
}
View<LinkComponent> LinkSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(LinkComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<LinkComponent>(result->second.second));
}
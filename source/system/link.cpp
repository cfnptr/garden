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

#include "garden/system/link.hpp"
#include "garden/system/log.hpp"

using namespace garden;

static void eraseEntityTag(LinkSystem::TagMap& tagMap, ID<Entity> entity, string_view tag)
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
void LinkComponent::regenerateUUID()
{
	auto linkSystem = LinkSystem::Instance::get();
	if (uuid)
	{
		auto result = linkSystem->uuidMap.erase(uuid);
		GARDEN_ASSERT_MSG(result == 1, "Detected memory corruption");
	}

	auto& randomDevice = linkSystem->randomDevice;
	uint32 seed[2] { randomDevice(), randomDevice() };
	uuid = Hash128::generateRandom(*(uint64*)(seed));
	auto result = linkSystem->uuidMap.emplace(uuid, entity);
	
	if (!result.second)
		throw GardenError("Link UUID collision occurred.");
}
bool LinkComponent::trySetUUID(const Hash128& uuid)
{
	if (this->uuid == uuid)
		return true;

	if (uuid)
	{
		auto linkSystem = LinkSystem::Instance::get();
		auto result = linkSystem->uuidMap.emplace(uuid, entity);
		if (!result.second)
			return false;
	}
	if (this->uuid)
	{
		auto linkSystem = LinkSystem::Instance::get();
		auto result = linkSystem->uuidMap.erase(this->uuid);
		GARDEN_ASSERT_MSG(result == 1, "Detected memory corruption");
	}

	this->uuid = uuid;
	return true;
}
void LinkComponent::setTag(string_view tag)
{
	if (this->tag == tag)
		return;

	auto linkSystem = LinkSystem::Instance::get();
	if (!this->tag.empty())
		eraseEntityTag(linkSystem->tagMap, entity, this->tag);
	if (!tag.empty())
		linkSystem->tagMap.emplace(tag, entity);

	this->tag = tag;
}

//**********************************************************************************************************************
LinkSystem::LinkSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);
}
LinkSystem::~LinkSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);
	unsetSingleton();
}

void LinkSystem::resetComponent(View<Component> component, bool full)
{
	auto linkView = View<LinkComponent>(component);
	if (linkView->uuid)
	{
		auto result = uuidMap.erase(linkView->uuid);
		GARDEN_ASSERT_MSG(result == 1, "Detected memory corruption");
		linkView->uuid = {};
	}
	if (!linkView->tag.empty())
	{
		eraseEntityTag(tagMap, linkView->entity, linkView->tag);
		linkView->tag = {};
	}
}
void LinkSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<LinkComponent>(source);
	auto destinationView = View<LinkComponent>(destination);

	if (sourceView->uuid)
		destinationView->regenerateUUID();

	if (!sourceView->tag.empty())
	{
		tagMap.emplace(sourceView->tag, destinationView->entity);
		destinationView->tag = sourceView->tag;
	}
}
string_view LinkSystem::getComponentName() const
{
	return "Link";
}

//**********************************************************************************************************************
void LinkSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto linkView = View<LinkComponent>(component);

	if (linkView->uuid)
	{
		linkView->uuid.toBase64(uuidStringCache);
		serializer.write("uuid", uuidStringCache);
	}

	if (!linkView->tag.empty())
		serializer.write("tag", linkView->tag);
}
void LinkSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto linkView = View<LinkComponent>(component);

	if (deserializer.read("uuid", uuidStringCache))
	{
		if (!linkView->uuid.fromBase64(uuidStringCache))
			GARDEN_LOG_ERROR("Deserialized entity with invalid link uuid. (uuid: " + uuidStringCache + ")");

		auto result = uuidMap.emplace(linkView->uuid, linkView->entity);
		if (!result.second)
		{
			GARDEN_LOG_ERROR("Deserialized entity with already existing link uuid. (uuid: " + uuidStringCache + ")");
			linkView->uuid = {};
		}
	}

	string tag;
	if (deserializer.read("tag", tag))
	{
		tagMap.emplace(tag, linkView->entity);
		linkView->tag = std::move(tag);
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
void LinkSystem::findEntities(string_view tag, vector<ID<Entity>>& entities) const
{
	GARDEN_ASSERT(!tag.empty());
	auto result = tagMap.equal_range(tag);
	for (auto i = result.first; i != result.second; i++)
		entities.emplace_back(i->second);
}
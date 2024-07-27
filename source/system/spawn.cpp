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

#include "garden/system/spawn.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/link.hpp"

using namespace garden;

//**********************************************************************************************************************
bool SpawnComponent::destroy()
{
	destroySpawned();
	return true;
}

ID<Entity> SpawnComponent::loadPrefab()
{
	if (prefab)
	{
		auto entity = LinkSystem::getInstance()->findEntity(prefab);
		if (entity)
			return entity;
	}

	if (path.empty())
		return {};

	auto spawnSystem = SpawnSystem::getInstance();
	ID<Entity> entity;

	auto pathString = path.generic_string();
	if (spawnSystem->tryGetSharedPrefab(pathString, prefab, entity))
		return entity;

	entity = ResourceSystem::getInstance()->loadScene(path, true);
	if (entity)
	{
		auto manager = Manager::getInstance();
		auto transformView = TransformSystem::getInstance()->get(entity);
		transformView->isActive = false;
		auto linkView = manager->add<LinkComponent>(entity);
		linkView->regenerateUUID();
		prefab = linkView->getUUID();
		manager->add<DoNotSerializeComponent>(entity);
		spawnSystem->addSharedPrefab(pathString, prefab);
	}
	return entity;
}

//**********************************************************************************************************************
void SpawnComponent::spawn(uint32 count)
{
	if (!count)
		return;

	auto prefabEntity = loadPrefab();
	if (!prefabEntity)
		return;

	auto manager = Manager::getInstance();
	auto linkSystem = LinkSystem::getInstance();
	auto transformSystem = TransformSystem::getInstance();

	for (uint32 i = 0; i < count; i++)
	{
		auto duplicateEntity = transformSystem->duplicateRecursive(prefabEntity);

		auto dupTransformView = transformSystem->tryGet(duplicateEntity);
		if (dupTransformView)
		{
			auto thisTransformView = transformSystem->tryGet(entity);
			if (thisTransformView)
			{
				auto model = thisTransformView->calcModel();
				extractTransform(model, dupTransformView->position,
					dupTransformView->scale, dupTransformView->rotation);
			}
			dupTransformView->setParent({});
			dupTransformView->isActive = true;
		}

		auto dupLinkView = linkSystem->tryGet(duplicateEntity);
		if (!dupLinkView)
			dupLinkView = manager->add<LinkComponent>(duplicateEntity);
		if (!dupLinkView->getUUID())
			dupLinkView->regenerateUUID();
		if (!DoNotSerializeSystem::getInstance()->has(duplicateEntity))
			manager->add<DoNotSerializeComponent>(duplicateEntity);
		spawnedEntities.push_back(dupLinkView->getUUID());
	}
}
void SpawnComponent::destroySpawned()
{
	auto linkSystem = LinkSystem::getInstance();
	auto transformSystem = TransformSystem::getInstance();
	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->findEntity(uuid);
		transformSystem->destroyRecursive(entity);
	}
	spawnedEntities.clear();
}

//**********************************************************************************************************************
SpawnSystem* SpawnSystem::instance = nullptr;

SpawnSystem::SpawnSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("PostDeinit", SpawnSystem::postDeinit);
	SUBSCRIBE_TO_EVENT("Update", SpawnSystem::update);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
SpawnSystem::~SpawnSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", SpawnSystem::postDeinit);
		UNSUBSCRIBE_FROM_EVENT("Update", SpawnSystem::update);
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void SpawnSystem::postDeinit()
{
	components.clear();
}

//**********************************************************************************************************************
void SpawnSystem::update()
{
	auto manager = Manager::getInstance();
	auto linkSystem = LinkSystem::getInstance();
	auto transformSystem = TransformSystem::getInstance();
	auto componentData = components.getData();
	auto occupancy = components.getOccupancy();

	for (uint32 i = 0; i < occupancy; i++)
	{
		auto componentView = &componentData[i];
		if (!componentView->isActive || (componentView->path.empty() && !componentView->prefab))
			continue;

		auto transformView = transformSystem->tryGet(componentView->entity);
		if (transformView && !transformView->isActive)
			continue;

		auto mode = componentView->mode;
		if (mode == SpawnMode::OneShot)
		{
			// TODO: take into account delay

			auto difference = (int64)componentView->maxCount - componentView->spawnedEntities.size();
			if (difference > 0)
				componentView->spawn(difference);
		}
	}
}

//**********************************************************************************************************************
ID<Component> SpawnSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void SpawnSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<SpawnComponent>(instance));
}
void SpawnSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<SpawnComponent>(source);
	auto destinationView = View<SpawnComponent>(destination);
	destinationView->path = sourceView->path;
	destinationView->prefab = sourceView->prefab;
	destinationView->maxCount = sourceView->maxCount;
	destinationView->delay = sourceView->delay;
	destinationView->mode = sourceView->mode;
	destinationView->isActive = sourceView->isActive;
}
const string& SpawnSystem::getComponentName() const
{
	static const string name = "Spawn";
	return name;
}
type_index SpawnSystem::getComponentType() const
{
	return typeid(SpawnComponent);
}
View<Component> SpawnSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<SpawnComponent>(instance)));
}
void SpawnSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
void SpawnSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<SpawnComponent>(component);

	if (!componentView->isActive)
		serializer.write("isActive", false);	
	if (!componentView->path.empty())
		serializer.write("path", componentView->path.generic_string());

	if (componentView->prefab)
	{
		auto entity = LinkSystem::getInstance()->findEntity(componentView->prefab);
		if (entity && !Manager::getInstance()->has<DoNotSerializeComponent>(entity))
			serializer.write("prefab", componentView->prefab.toBase64());
	}

	if (componentView->maxCount != 1)
		serializer.write("maxCount", componentView->maxCount);
	if (componentView->delay != 0.0f)
		serializer.write("delay", componentView->delay);

	if (componentView->mode != SpawnMode::OneShot)
	{
		switch (componentView->mode)
		{
		default:
			break;
		}
	}
}
void SpawnSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<SpawnComponent>(component);
	deserializer.read("isActive", componentView->isActive);

	string stringValue;
	if (deserializer.read("path", stringValue))
		componentView->path = stringValue;
	if (deserializer.read("prefab", stringValue))
		componentView->prefab.fromBase64(stringValue);

	deserializer.read("maxCount", componentView->maxCount);
	deserializer.read("delay", componentView->delay);

	if (deserializer.read("mode", stringValue))
	{
	}
}

//**********************************************************************************************************************
bool SpawnSystem::tryAddSharedPrefab(const string& path, const Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(uuid);
	auto linkSystem = LinkSystem::getInstance();

	auto searchResult = sharedPrefabs.find(path);
	if (searchResult != sharedPrefabs.end())
	{
		if (linkSystem->findEntity(searchResult->second))
			return false;
		searchResult->second = uuid;
		return true;
	}

	auto emplaceResult = sharedPrefabs.emplace(path, uuid);
	GARDEN_ASSERT(emplaceResult.second); // Corrupted memory.
	return true;
}
bool SpawnSystem::tryAddSharedPrefab(const string& path, ID<Entity> prefab)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(prefab);
	auto linkSystem = LinkSystem::getInstance();

	auto searchResult = sharedPrefabs.find(path);
	if (searchResult != sharedPrefabs.end())
	{
		if (linkSystem->findEntity(searchResult->second))
			return false;
	}

	auto linkView = linkSystem->tryGet(prefab);
	if (linkView)
	{
		if (!linkView->getUUID())
			linkView->regenerateUUID();
	}
	else
	{
		linkView = Manager::getInstance()->add<LinkComponent>(prefab);
		linkView->regenerateUUID();
	}
	
	if (searchResult == sharedPrefabs.end())
	{
		auto emplaceResult = sharedPrefabs.emplace(path, linkView->getUUID());
		GARDEN_ASSERT(emplaceResult.second); // Corrupted memory.
	}
	else
	{
		searchResult->second = linkView->getUUID();
	}
	return true;
}

//**********************************************************************************************************************
bool SpawnSystem::tryGetSharedPrefab(const string& path, Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end() ||
		!LinkSystem::getInstance()->findEntity(searchResult->second))
	{
		return false;
	}
	uuid = searchResult->second;
	return true;
}
bool SpawnSystem::tryGetSharedPrefab(const string& path, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::getInstance()->findEntity(searchResult->second);
	if (!entity)
		return false;

	prefab = entity;
	return true;
}
bool SpawnSystem::tryGetSharedPrefab(const string& path, Hash128& uuid, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::getInstance()->findEntity(searchResult->second);
	if (!entity)
		return false;

	uuid = searchResult->second;
	prefab = entity;
	return true;
}

void SpawnSystem::destroySharedPrefabs()
{
	auto manager = Manager::getInstance();
	auto linkSystem = LinkSystem::getInstance();
	auto transformSystem = TransformSystem::getInstance();

	for (const auto& pair : sharedPrefabs)
	{
		auto prefab = linkSystem->findEntity(pair.second);
		if (!prefab)
			continue;
		transformSystem->destroyRecursive(prefab);
	}
	sharedPrefabs.clear();
}

//**********************************************************************************************************************
bool SpawnSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(SpawnComponent)) != entityComponents.end();
}
View<SpawnComponent> SpawnSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(SpawnComponent));
	return components.get(ID<SpawnComponent>(pair.second));
}
View<SpawnComponent> SpawnSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(SpawnComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<SpawnComponent>(result->second.second));
}
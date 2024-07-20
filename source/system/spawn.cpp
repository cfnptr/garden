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

void SpawnComponent::destroySpawned()
{
	auto manager = Manager::getInstance();
	auto linkSystem = LinkSystem::getInstance();

	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->findEntity(uuid);
		manager->destroy(entity);
	}
	spawnedEntities.clear();
}

//**********************************************************************************************************************
SpawnSystem* SpawnSystem::instance = nullptr;

SpawnSystem::SpawnSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Update", SpawnSystem::update);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
SpawnSystem::~SpawnSystem()
{
	components.clear();

	auto manager = Manager::getInstance();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("Update", SpawnSystem::update);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
/*void loadPrefab() // TODO:
{
	if (path.empty())
		return;

	auto linkSystem = LinkSystem::getInstance();
	auto entity = linkSystem->findEntity(prefab);
	if (entity)
		return;

	// TODO: we can share 
	entity = ResourceSystem::getInstance()->loadScene(path, true);
	auto transformView = TransformSystem::getInstance()->get(entity);
	transformView->isActive = false;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformView->debugName = path;
	#endif

	auto linkView = Manager::getInstance()->add<LinkComponent>(entity);
	linkView->regenerateUUID();
	prefab = linkView->getUUID();
}*/

void SpawnSystem::update()
{
	auto componentData = components.getData();
	auto occupancy = components.getOccupancy();

	for (uint32 i = 0; i < occupancy; i++)
	{
		auto& component = componentData[i];
		if (component.path.empty())
			continue;

		auto mode = component.mode;
		if (mode == SpawnMode::OneShot)
		{
			if (component.spawnedEntities.empty())
			{
				// component.preloadPrefab();

			}
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
	auto componentView = View<LinkComponent>(component);

}
void SpawnSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<LinkComponent>(component);

}

//**********************************************************************************************************************
bool SpawnSystem::tryAddSharedPrefab(const string& path, const Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(uuid);
	return sharedPrefabs.emplace(path, uuid).second;
}
bool SpawnSystem::tryAddSharedPrefab(const string& path, ID<Entity> prefab)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(prefab);
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult != sharedPrefabs.end())
		return false;

	auto linkSystem = LinkSystem::getInstance();
	auto linkComponent = linkSystem->tryGet(prefab);
	if (!linkComponent)
		linkComponent = Manager::getInstance()->add<LinkComponent>(prefab);
	if (!linkComponent->getUUID())
		linkComponent->regenerateUUID();
	auto emplaceResult = sharedPrefabs.emplace(path, linkComponent->getUUID());
	GARDEN_ASSERT(emplaceResult.second); // Corrupted memory.
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
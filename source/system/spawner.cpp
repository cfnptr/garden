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

#include "garden/system/spawner.hpp"
#include "garden/system/link.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/character.hpp"

using namespace garden;

//**********************************************************************************************************************
bool SpawnerComponent::destroy()
{
	destroySpawned();
	return true;
}

ID<Entity> SpawnerComponent::loadPrefab()
{
	if (prefab)
	{
		auto entity = LinkSystem::get()->findEntity(prefab);
		if (entity)
			return entity;
	}

	if (path.empty())
		return {};

	auto spawnerSystem = SpawnerSystem::get();
	ID<Entity> entity;

	auto pathString = path.generic_string();
	if (spawnerSystem->tryGetSharedPrefab(pathString, prefab, entity))
		return entity;

	entity = ResourceSystem::get()->loadScene(path, true);
	if (entity)
	{
		auto manager = Manager::get();
		manager->add<DoNotSerializeComponent>(entity);

		auto transformView = TransformSystem::get()->get(entity);
		transformView->isActive = false;
		auto prefabs = LinkSystem::get()->findEntities("Prefabs");
		if (prefabs.first != prefabs.second)
			transformView->setParent(prefabs.first->second);

		auto linkView = manager->add<LinkComponent>(entity);
		linkView->regenerateUUID();
		prefab = linkView->getUUID();

		spawnerSystem->addSharedPrefab(pathString, prefab);
	}
	return entity;
}

//**********************************************************************************************************************
void SpawnerComponent::spawn(uint32 count)
{
	if (!count)
		return;

	auto prefabEntity = loadPrefab();
	if (!prefabEntity)
		return;

	auto manager = Manager::get();
	auto linkSystem = LinkSystem::get();
	auto transformSystem = TransformSystem::get();
	auto physicsSystem = manager->tryGet<PhysicsSystem>();
	auto characterSystem = manager->tryGet<CharacterSystem>();

	for (uint32 i = 0; i < count; i++)
	{
		auto duplicateEntity = transformSystem->duplicateRecursive(prefabEntity);

		// if (physicsSystem)... TODO: Duplicate constraints recursive.

		auto dupTransformView = transformSystem->tryGet(duplicateEntity);
		if (dupTransformView)
		{
			if (spawnAsChild)
			{
				if (transformSystem->has(entity))
				{
					dupTransformView->position = float3(0.0f);
					dupTransformView->scale = float3(1.0f);
					dupTransformView->rotation = float3(0.0f);
					dupTransformView->setParent(entity);
				}
				else
				{
					dupTransformView->setParent({});
				}
			}
			else
			{
				auto thisTransformView = transformSystem->tryGet(entity);
				if (thisTransformView)
				{
					auto model = thisTransformView->calcModel();
					extractTransform(model, dupTransformView->position,
						dupTransformView->scale, dupTransformView->rotation);
				}
				dupTransformView->setParent({});
			}
			
			dupTransformView->isActive = true;
		}

		if (physicsSystem)
			physicsSystem->setWorldTransformRecursive(duplicateEntity);
		if (characterSystem)
			characterSystem->setWorldTransformRecursive(duplicateEntity);

		auto dupLinkView = linkSystem->tryGet(duplicateEntity);
		if (!dupLinkView)
			dupLinkView = manager->add<LinkComponent>(duplicateEntity);
		if (!dupLinkView->getUUID())
			dupLinkView->regenerateUUID();
		if (!DoNotSerializeSystem::get()->has(duplicateEntity))
			manager->add<DoNotSerializeComponent>(duplicateEntity);
		spawnedEntities.push_back(dupLinkView->getUUID());
	}

	if (delay != 0.0f)
		delayTime = InputSystem::get()->getTime() + delay;
}
void SpawnerComponent::destroySpawned()
{
	auto linkSystem = LinkSystem::get();
	auto transformSystem = TransformSystem::get();
	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->findEntity(uuid);
		transformSystem->destroyRecursive(entity);
	}
	spawnedEntities.clear();
}

//**********************************************************************************************************************
SpawnerSystem* SpawnerSystem::instance = nullptr;

SpawnerSystem::SpawnerSystem()
{
	auto manager = Manager::get();
	SUBSCRIBE_TO_EVENT("PreInit", SpawnerSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostDeinit", SpawnerSystem::postDeinit);
	SUBSCRIBE_TO_EVENT("Update", SpawnerSystem::update);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
SpawnerSystem::~SpawnerSystem()
{
	auto manager = Manager::get();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", SpawnerSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", SpawnerSystem::postDeinit);
		UNSUBSCRIBE_FROM_EVENT("Update", SpawnerSystem::update);
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void SpawnerSystem::preInit()
{
	auto manager = Manager::get();
	auto prefabs = manager->createEntity();

	if (manager->has<DoNotDestroySystem>())
		manager->add<DoNotDestroyComponent>(prefabs);
	if (manager->has<DoNotSerializeSystem>())
		manager->add<DoNotSerializeComponent>(prefabs);

	auto transformView = manager->add<TransformComponent>(prefabs);
	transformView->isActive = false;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformView->debugName = "Prefabs";
	#endif
	
	auto linkView = manager->add<LinkComponent>(prefabs);
	linkView->setTag("Prefabs");
}
void SpawnerSystem::postDeinit()
{
	components.clear();
}

//**********************************************************************************************************************
void SpawnerSystem::update()
{
	auto manager = Manager::get();
	auto linkSystem = LinkSystem::get();
	auto transformSystem = TransformSystem::get();
	auto currentTime = InputSystem::get()->getTime();
	auto componentData = components.getData();
	auto occupancy = components.getOccupancy();

	for (uint32 i = 0; i < occupancy; i++)
	{
		auto componentView = &componentData[i];
		if (!componentView->isActive || (componentView->path.empty() && !componentView->prefab))
			continue;

		auto transformView = transformSystem->tryGet(componentView->entity);
		if (transformView && !transformView->isActiveWithAncestors())
			continue;

		if (componentView->delay != 0.0f)
		{
			if (componentView->delayTime > currentTime)
				continue;
		}

		auto mode = componentView->mode;
		if (mode == SpawnMode::OneShot)
		{
			auto difference = (int64)componentView->maxCount - componentView->spawnedEntities.size();
			if (difference > 0)
				componentView->spawn(difference);
		}
	}
}

//**********************************************************************************************************************
ID<Component> SpawnerSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void SpawnerSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<SpawnerComponent>(instance));
}
void SpawnerSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<SpawnerComponent>(source);
	auto destinationView = View<SpawnerComponent>(destination);
	destinationView->destroy();

	destinationView->path = sourceView->path;
	destinationView->prefab = sourceView->prefab;
	destinationView->maxCount = sourceView->maxCount;
	destinationView->delay = sourceView->delay;
	destinationView->mode = sourceView->mode;
	destinationView->isActive = sourceView->isActive;
	destinationView->spawnAsChild = sourceView->spawnAsChild;
}
const string& SpawnerSystem::getComponentName() const
{
	static const string name = "Spawner";
	return name;
}
type_index SpawnerSystem::getComponentType() const
{
	return typeid(SpawnerComponent);
}
View<Component> SpawnerSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<SpawnerComponent>(instance)));
}
void SpawnerSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
void SpawnerSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<SpawnerComponent>(component);

	if (!componentView->isActive)
		serializer.write("isActive", false);	
	if (!componentView->path.empty())
		serializer.write("path", componentView->path.generic_string());

	if (componentView->prefab)
	{
		auto entity = LinkSystem::get()->findEntity(componentView->prefab);
		if (entity && !Manager::get()->has<DoNotSerializeComponent>(entity))
		{
			componentView->prefab.toBase64(valueStringCache);
			serializer.write("prefab", valueStringCache);
		}
	}

	if (componentView->maxCount != 1)
		serializer.write("maxCount", componentView->maxCount);
	if (componentView->delay != 0.0f)
		serializer.write("delay", componentView->delay);
	if (!componentView->spawnAsChild)
		serializer.write("spawnAsChild", false);

	switch (componentView->mode)
	{
	case SpawnMode::Manual:
		serializer.write("mode", string_view("Manual"));
		break;
	default:
		break;
	}
}
void SpawnerSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<SpawnerComponent>(component);
	deserializer.read("isActive", componentView->isActive);

	if (deserializer.read("path", valueStringCache))
		componentView->path = valueStringCache;
	if (deserializer.read("prefab", valueStringCache))
		componentView->prefab.fromBase64(valueStringCache);

	deserializer.read("maxCount", componentView->maxCount);
	deserializer.read("delay", componentView->delay);
	deserializer.read("spawnAsChild", componentView->spawnAsChild);

	if (deserializer.read("mode", valueStringCache))
	{
		if (valueStringCache == "Manual")
			componentView->mode = SpawnMode::Manual;
	}
}

//**********************************************************************************************************************
bool SpawnerSystem::tryAddSharedPrefab(const string& path, const Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(uuid);
	auto linkSystem = LinkSystem::get();

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
bool SpawnerSystem::tryAddSharedPrefab(const string& path, ID<Entity> prefab)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(prefab);
	auto linkSystem = LinkSystem::get();

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
		linkView = Manager::get()->add<LinkComponent>(prefab);
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
bool SpawnerSystem::tryGetSharedPrefab(const string& path, Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end() ||
		!LinkSystem::get()->findEntity(searchResult->second))
	{
		return false;
	}
	uuid = searchResult->second;
	return true;
}
bool SpawnerSystem::tryGetSharedPrefab(const string& path, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::get()->findEntity(searchResult->second);
	if (!entity)
		return false;

	prefab = entity;
	return true;
}
bool SpawnerSystem::tryGetSharedPrefab(const string& path, Hash128& uuid, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::get()->findEntity(searchResult->second);
	if (!entity)
		return false;

	uuid = searchResult->second;
	prefab = entity;
	return true;
}

void SpawnerSystem::destroySharedPrefabs()
{
	auto manager = Manager::get();
	auto linkSystem = LinkSystem::get();
	auto transformSystem = TransformSystem::get();

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
bool SpawnerSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(SpawnerComponent)) != entityComponents.end();
}
View<SpawnerComponent> SpawnerSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(SpawnerComponent));
	return components.get(ID<SpawnerComponent>(pair.second));
}
View<SpawnerComponent> SpawnerSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(SpawnerComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<SpawnerComponent>(result->second.second));
}
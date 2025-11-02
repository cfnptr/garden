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

#include "garden/system/spawner.hpp"
#include "garden/system/link.hpp"
#include "garden/system/loop.hpp"
#include "garden/system/input.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/character.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"

using namespace garden;

ID<Entity> SpawnerComponent::loadPrefab()
{
	if (prefab)
	{
		auto entity = LinkSystem::Instance::get()->findEntity(prefab);
		if (entity)
			return entity;
	}

	if (path.empty())
		return {};

	auto spawnerSystem = SpawnerSystem::Instance::get();
	ID<Entity> entity;

	auto pathString = path.generic_string();
	if (spawnerSystem->tryGetSharedPrefab(pathString, prefab, entity))
		return entity;

	entity = ResourceSystem::Instance::get()->loadScene(path, true);
	if (entity)
	{
		auto manager = Manager::Instance::get();
		manager->add<DoNotSerializeComponent>(entity);

		auto transformView = manager->get<TransformComponent>(entity);
		transformView->setActive(false);

		auto prefabs = LinkSystem::Instance::get()->findEntities("Prefabs");
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

	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	auto physicsSystem = PhysicsSystem::Instance::tryGet();
	auto characterSystem = CharacterSystem::Instance::tryGet();

	for (uint32 i = 0; i < count; i++)
	{
		auto duplicateEntity = transformSystem->duplicateRecursive(prefabEntity);

		// if (physicsSystem)... TODO: Duplicate constraints recursive.

		auto dupTransformView = manager->tryGet<TransformComponent>(duplicateEntity);
		if (dupTransformView)
		{
			if (spawnAsChild)
			{
				if (manager->has<TransformComponent>(entity))
				{
					dupTransformView->setPosition(f32x4::zero);
					dupTransformView->setScale(f32x4::one);
					dupTransformView->setRotation(quat::identity);
					dupTransformView->setParent(entity);
				}
				else
				{
					dupTransformView->setParent({});
				}
			}
			else
			{
				auto thisTransformView = manager->tryGet<TransformComponent>(entity);
				if (thisTransformView)
				{
					auto model = thisTransformView->calcModel();
					f32x4 position, scale; quat rotation;
					extractTransform(model, position, rotation, scale);
					dupTransformView->setPosition(position);
					dupTransformView->setScale(scale);
					dupTransformView->setRotation(rotation);
				}
				dupTransformView->setParent({});
			}
			
			dupTransformView->setActive(true);
		}

		if (physicsSystem)
			physicsSystem->setWorldTransformRecursive(duplicateEntity);
		if (characterSystem)
			characterSystem->setWorldTransformRecursive(duplicateEntity);

		auto dupLinkView = manager->getOrAdd<LinkComponent>(duplicateEntity);
		if (!dupLinkView->getUUID())
			dupLinkView->regenerateUUID();
		if (!manager->has<DoNotSerializeComponent>(duplicateEntity))
			manager->add<DoNotSerializeComponent>(duplicateEntity);
		spawnedEntities.push_back(dupLinkView->getUUID());
	}

	if (delay != 0.0f)
		delayTime = InputSystem::Instance::get()->getCurrentTime() + delay;
}
void SpawnerComponent::destroySpawned()
{
	if (spawnedEntities.empty())
		return;

	auto linkSystem = LinkSystem::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	for (const auto& uuid : spawnedEntities)
	{
		auto entity = linkSystem->findEntity(uuid);
		transformSystem->destroyRecursive(entity);
	}
	spawnedEntities.clear();
}

//**********************************************************************************************************************
SpawnerSystem::SpawnerSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", SpawnerSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("Update", SpawnerSystem::update);
}
SpawnerSystem::~SpawnerSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);

		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", SpawnerSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", SpawnerSystem::update);
	}

	unsetSingleton();
}

void SpawnerSystem::preInit()
{
	auto manager = Manager::Instance::get();
	auto prefabs = manager->createEntity();
	manager->reserveComponents(prefabs, 2);

	auto transformView = manager->add<TransformComponent>(prefabs);
	transformView->setActive(false);
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformView->debugName = "Prefabs";
	#endif
	
	auto linkView = manager->add<LinkComponent>(prefabs);
	linkView->setTag("Prefabs");

	if (DoNotDestroySystem::Instance::has())
		manager->add<DoNotDestroyComponent>(prefabs);
	if (DoNotSerializeSystem::Instance::has())
		manager->add<DoNotSerializeComponent>(prefabs);
}

//**********************************************************************************************************************
void SpawnerSystem::update()
{
	SET_CPU_ZONE_SCOPED("Spawners Update");

	double currentTime;
	auto inputSystem = InputSystem::Instance::tryGet();
	if (inputSystem) currentTime = inputSystem->getCurrentTime();
	else currentTime = LoopSystem::Instance::get()->getCurrentTime();

	auto manager = Manager::Instance::get();
	for (auto& spawner : components)
	{
		if (!spawner.getEntity() || !spawner.isActive || (spawner.path.empty() && !spawner.prefab))
			continue;

		auto transformView = manager->tryGet<TransformComponent>(spawner.getEntity());
		if (transformView && !transformView->isActive())
			continue;

		if (spawner.delay != 0.0f)
		{
			if (spawner.delayTime > currentTime)
				continue;
		}

		auto mode = spawner.mode;
		if (mode == SpawnMode::OneShot)
		{
			auto difference = (int64)spawner.maxCount - spawner.spawnedEntities.size();
			if (difference > 0)
				spawner.spawn(difference);
		}
	}
}

//**********************************************************************************************************************
void SpawnerSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<SpawnerComponent>(component);
	componentView->destroySpawned();

	if (full)
		**componentView = {};
}
void SpawnerSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<SpawnerComponent>(source);
	auto destinationView = View<SpawnerComponent>(destination);
	destinationView->path = sourceView->path;
	destinationView->prefab = sourceView->prefab;
	destinationView->maxCount = sourceView->maxCount;
	destinationView->delay = sourceView->delay;
	destinationView->mode = sourceView->mode;
	destinationView->isActive = sourceView->isActive;
	destinationView->spawnAsChild = sourceView->spawnAsChild;
}
string_view SpawnerSystem::getComponentName() const
{
	return "Spawner";
}

//**********************************************************************************************************************
void SpawnerSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<SpawnerComponent>(component);
	if (!componentView->isActive)
		serializer.write("isActive", false);	
	if (!componentView->path.empty())
		serializer.write("path", componentView->path.generic_string());

	if (componentView->prefab)
	{
		auto entity = LinkSystem::Instance::get()->findEntity(componentView->prefab);
		if (entity && !Manager::Instance::get()->has<DoNotSerializeComponent>(entity))
		{
			componentView->prefab.toBase64URL(valueStringCache);
			serializer.write("prefab", valueStringCache);
		}
	}

	if (componentView->maxCount != 1)
		serializer.write("maxCount", componentView->maxCount);
	if (componentView->delay != 0.0f)
		serializer.write("delay", componentView->delay);
	if (!componentView->spawnAsChild)
		serializer.write("spawnAsChild", false);
	if (componentView->mode != SpawnMode::OneShot)
		serializer.write("mode", toString(componentView->mode));
}
void SpawnerSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<SpawnerComponent>(component);
	deserializer.read("isActive", componentView->isActive);

	if (deserializer.read("path", valueStringCache))
		componentView->path = valueStringCache;
	if (deserializer.read("prefab", valueStringCache))
		componentView->prefab.fromBase64URL(valueStringCache);

	deserializer.read("maxCount", componentView->maxCount);
	deserializer.read("delay", componentView->delay);
	deserializer.read("spawnAsChild", componentView->spawnAsChild);

	if (deserializer.read("mode", valueStringCache))
		toSpawnMode(valueStringCache, componentView->mode);
}

//**********************************************************************************************************************
bool SpawnerSystem::tryAddSharedPrefab(string_view path, const Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(uuid, "Assert " + string(path));
	auto linkSystem = LinkSystem::Instance::get();

	auto searchResult = sharedPrefabs.find(path);
	if (searchResult != sharedPrefabs.end())
	{
		if (linkSystem->findEntity(searchResult->second))
			return false;
		searchResult.value() = uuid;
		return true;
	}

	auto emplaceResult = sharedPrefabs.emplace(path, uuid);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return true;
}
bool SpawnerSystem::tryAddSharedPrefab(string_view path, ID<Entity> prefab)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(prefab, "Assert " + string(path));
	auto linkSystem = LinkSystem::Instance::get();

	auto searchResult = sharedPrefabs.find(path);
	if (searchResult != sharedPrefabs.end())
	{
		if (linkSystem->findEntity(searchResult->second))
			return false;
	}

	auto linkView = Manager::Instance::get()->getOrAdd<LinkComponent>(prefab);
	if (!linkView->getUUID())
		linkView->regenerateUUID();
	
	if (searchResult == sharedPrefabs.end())
	{
		auto emplaceResult = sharedPrefabs.emplace(path, linkView->getUUID());
		GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	}
	else
	{
		searchResult.value() = linkView->getUUID();
	}
	return true;
}

//**********************************************************************************************************************
bool SpawnerSystem::tryGetSharedPrefab(string_view path, Hash128& uuid)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end() ||
		!LinkSystem::Instance::get()->findEntity(searchResult->second))
	{
		return false;
	}
	uuid = searchResult->second;
	return true;
}
bool SpawnerSystem::tryGetSharedPrefab(string_view path, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::Instance::get()->findEntity(searchResult->second);
	if (!entity)
		return false;

	prefab = entity;
	return true;
}
bool SpawnerSystem::tryGetSharedPrefab(string_view path, Hash128& uuid, ID<Entity>& prefab)
{
	GARDEN_ASSERT(!path.empty());
	auto searchResult = sharedPrefabs.find(path);
	if (searchResult == sharedPrefabs.end())
		return false;

	auto entity = LinkSystem::Instance::get()->findEntity(searchResult->second);
	if (!entity)
		return false;

	uuid = searchResult->second;
	prefab = entity;
	return true;
}

void SpawnerSystem::destroySharedPrefabs()
{
	auto linkSystem = LinkSystem::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();

	for (const auto& pair : sharedPrefabs)
	{
		auto prefab = linkSystem->findEntity(pair.second);
		if (!prefab)
			continue;
		transformSystem->destroyRecursive(prefab);
	}
	sharedPrefabs.clear();
}
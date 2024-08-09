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

#pragma once
#include "garden/serialize.hpp"
#include "garden/hash.hpp"
#include "ecsm.hpp"

namespace garden
{

using namespace ecsm;
class SpawnerSystem;

enum class SpawnMode : uint8
{
	OneShot,
	Manual,
	Count
};

// TODO: add instace pool mode.

/***********************************************************************************************************************
 * @brief Contains information about objects spawn point and spawning mode.
 */
struct SpawnerComponent final : public Component
{
	fs::path path = {};   /**< Target prefab scene path */
	Hash128 prefab = {};  /**< Target runtime prefab object UUID */
	uint32 maxCount = 1;  /**< Maximal automatic object spawn count */
	float delay = 0.0f;   /**< Delay before next object spawn (seconds) */
	SpawnMode mode = {};  /**< Automatic object spawn mode */
	bool isActive = true; /**< Is spawn component active */
	bool spawnAsChild = true;  /**< Spawn object as a spawner entity child */
private:
	uint8 _alignment = 0;
	vector<Hash128> spawnedEntities;

	bool destroy();

	friend class SpawnerSystem;
	friend class LinearPool<SpawnerComponent>;
public:
	/**
	 * @brief Returns spawned enitity array.
	 */
	const vector<Hash128>& getSpawnedEntities() const noexcept { return spawnedEntities; }
	/**
	 * @brief Returns spawned enitity count.
	 * @note Some of the spawned entities may be already destroyed.
	 */
	uint32 getSpawnedCount() const noexcept { return (uint32)spawnedEntities.size(); }

	/**
	 * @brief Loads prefab entity. (Creates shared if not exist)
	 * @details Entity is loaded by path or by provided prefab UUID.
	 * @note If prefab entity was destroyed this call recreates it.
	 * @return Loaded prefab entity on success, otherwise null.
	 */
	ID<Entity> loadPrefab();
	/**
	 * @brief Spawns a new prefab instance.
	 * @note If prefab entity was destroyed this call recreates it.
	 * 
	 * @param count how many entity instances to spawn
	 * @return Loaded prefab entity on success, otherwise null.
	 */
	void spawn(uint32 count = 1);

	/**
	 * @brief Destroys all existing spawned entites.
	 * @note Some of the spawned entities may be already destroyed.
	 */
	void destroySpawned();
};

/***********************************************************************************************************************
 * @brief Provides spawning of pre-defined objects (prefabs) at runtime.
 */
class SpawnerSystem final : public System, public ISerializable
{
	LinearPool<SpawnerComponent> components;
	map<string, Hash128> sharedPrefabs;
	string valueStringCache;

	static SpawnerSystem* instance;

	/**
	 * @brief Creates a new spawner system instance.
	 */
	SpawnerSystem();
	/**
	 * @brief Destroys spawner system instance.
	 */
	~SpawnerSystem() final;

	void preInit();
	void postDeinit();
	void update();

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
	friend class LinkComponent;
public:
	/**
	 * @brief Returns spawner component pool.
	 */
	const LinearPool<SpawnerComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns shared prefab map.
	 */
	const map<string, Hash128>& getSharedPrefabs() const noexcept { return sharedPrefabs; }

	/**
	 * @brief Returns true if has shared prefab.
	 * @param[in] path target shared prefab path
	 */
	bool hasSharedPrefab(const string& path) const { return sharedPrefabs.find(path) != sharedPrefabs.end(); }

	/**
	 * @brief Adds shared prefab to the map.
	 * 
	 * @param[in] path target shared prefab path
	 * @param[in] uuid target shared prefab UUID
	 * 
	 * @return True if a new shared prefab was added to the map.
	 */
	bool tryAddSharedPrefab(const string& path, const Hash128& uuid);
	/**
	 * @brief Adds shared prefab to the map.
	 *
	 * @param[in] path target shared prefab path
	 * @param prefab target shared prefab entity
	 *
	 * @return True if a new shared prefab was added to the map.
	 */
	bool tryAddSharedPrefab(const string& path, ID<Entity> prefab);
	/**
	 * @brief Adds shared prefab to the map.
	 *
	 * @param[in] path target shared prefab path
	 * @param[in] uuid target shared prefab UUID
	 */
	void addSharedPrefab(const string& path, const Hash128& uuid)
	{
		auto result = tryAddSharedPrefab(path, uuid);
		GARDEN_ASSERT(result); // Shared prefab already exist
	}
	/**
	 * @brief Adds shared prefab to the map.
	 *
	 * @param[in] path target shared prefab path
	 * @param prefab target shared prefab entity
	 */
	void addSharedPrefab(const string& path, ID<Entity> prefab)
	{
		auto result = tryAddSharedPrefab(path, prefab);
		GARDEN_ASSERT(result); // Shared prefab already exist
	}

	/**
	 * @brief Returns shared prefab UUID if exist.
	 *
	 * @param[in] path target shared prefab path
	 * @param[out] uuid shared prefab UUID
	 *
	 * @return True if shared prefab exist.
	 */
	bool tryGetSharedPrefab(const string& path, Hash128& uuid);
	/**
	 * @brief Returns shared prefab entity if exist.
	 *
	 * @param[in] path target shared prefab path
	 * @param[out] prefab shared prefab entity
	 *
	 * @return True if shared prefab exist.
	 */
	bool tryGetSharedPrefab(const string& path, ID<Entity>& prefab);
	/**
	 * @brief Returns shared prefab UUD and entity if exist.
	 *
	 * @param[in] path target shared prefab path
	 * @param[out] uuid shared prefab UUID
	 * @param[out] prefab shared prefab entity
	 *
	 * @return True if shared prefab exist.
	 */
	bool tryGetSharedPrefab(const string& path, Hash128& uuid, ID<Entity>& prefab);

	/**
	 * @brief Destroys all existing shared prefab entities and clears the map.
	 */
	void destroySharedPrefabs();

	/**
	 * @brief Returns true if entity has spawner component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity spawner component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<SpawnerComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity spawner component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<SpawnerComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns spawner system instance.
	 * @warning Do not use it if you have several link system instances.
	 */
	static SpawnerSystem* get() noexcept
	{
		GARDEN_ASSERT(instance); // Spawn system is not created.
		return instance;
	}
};

} // namespace garden
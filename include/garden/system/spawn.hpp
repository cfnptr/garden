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
class SpawnSystem;

enum class SpawnMode : uint8
{
	OneShot,
	Count
};

struct SpawnComponent final : public Component
{
	string path = {};
	uint32 maxCount = 0;
	float delay = 0.0f;
	SpawnMode mode = {};
	bool isActive = true;
private:
	uint16 _alignment = 0;
	vector<Hash128> spawnedEntities;

	bool destroy();

	friend class SpawnSystem;
	friend class LinearPool<SpawnComponent>;
public:
	void destroySpawned();
};

class SpawnSystem final : public System, public ISerializable
{
	LinearPool<SpawnComponent> components;
	map<string, Hash128> sharedPrefabs;

	static SpawnSystem* instance;

	/**
	 * @brief Creates a new spawner system instance.
	 */
	SpawnSystem();
	/**
	 * @brief Destroys spawner system instance.
	 */
	~SpawnSystem() final;

	void update();

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
	friend class LinkComponent;
public:
	/**
	 * @brief Returns spawn component pool.
	 */
	const LinearPool<SpawnComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns shared prefab map.
	 */
	const map<string, Hash128>& getSharedPrefabs() const noexcept { return sharedPrefabs; }

	bool hasSharedPrefab(const string& path) const { return sharedPrefabs.find(path) != sharedPrefabs.end(); }
	bool tryAddSharedPrefab(const string& path, const Hash128& uuid);
	bool tryAddSharedPrefab(const string& path, ID<Entity> prefab);
	void destroySharedPrefabs();

	/**
	 * @brief Returns spawn system instance.
	 * @warning Do not use it if you have several link system instances.
	 */
	static SpawnSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // Spawn system is not created.
		return instance;
	}
};

} // namespace garden
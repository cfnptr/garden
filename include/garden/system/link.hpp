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
#include <random>

namespace garden
{

using namespace ecsm;
class LinkSystem;

struct LinkComponent final : public Component
{
private:
	Hash128 uuid = {};
	string tag = {};
	bool destroy();

	friend class LinkSystem;
	friend class LinearPool<LinkComponent>;
public:
	const Hash128& getUUID() const noexcept { return uuid; }
	const string& getTag() const noexcept { return tag; }

	void regenerateUUID();
	bool trySetUUID(const Hash128& uuid);
	void setTag(const string& tag);
};

class LinkSystem final : public System, public ISerializable
{
	LinearPool<LinkComponent> components;
	map<Hash128, ID<LinkComponent>> uuidMap;
	multimap<string, ID<LinkComponent>> tagMap;
	random_device randomDevice;

	static LinkSystem* instance;

	/**
	 * @brief Creates a new logging system instance.
	 */
	LinkSystem();
	/**
	 * @brief Destroys logging system instance.
	 */
	~LinkSystem() final;

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
	const LinearPool<LinkComponent>& getComponents() const noexcept { return components; }
	const map<Hash128, ID<LinkComponent>>& getUuidMap() const noexcept { return uuidMap; }
	const multimap<string, ID<LinkComponent>>& getTagMap() const noexcept { return tagMap; }

	ID<Entity> findEntity(const Hash128& uuid);

	auto findEntities(const string& tag) { return tagMap.equal_range(tag); }
	void findEntities(const string& tag, vector<ID<Entity>>& entities);

	/**
	 * @brief Returns link system instance.
	 * @warning Do not use it if you have several link system instances.
	 */
	static LinkSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // Link system is not created.
		return instance;
	}
};

} // namespace garden
//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/defines.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "garden/system/serialize.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
struct TransformComponent final : public Component
{
	float3 position = float3(0.0f);
	float3 scale = float3(1.0f);
	quat rotation = quat::identity;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	string name;
	#endif
private:
	ID<Entity> parent = {};
	uint32 childCount = 0;
	uint32 childCapacity = 0;
	ID<Entity> entity = {};
	ID<Entity>* childs = nullptr;
	Manager* manager = nullptr;

	bool destroy();

	friend class TransformSystem;
	friend class LinearPool<TransformComponent>;
public:
	ID<Entity> getParent() const noexcept { return parent; }
	uint32 getChildCount() const noexcept { return childCount; }
	const ID<Entity>* getChilds() const noexcept { return childs; }
	ID<Entity> getEntity() const noexcept { return entity; }

	float4x4 calcModel() const noexcept;
	void setParent(ID<Entity> parent);
	void addChild(ID<Entity> child);
	void removeChild(ID<Entity> child);
	void removeChild(uint32 index);
	void removeAllChilds();
	bool hasChild(ID<Entity> child) const noexcept;
	bool hasAncestor(ID<Entity> ancestor) const noexcept;
	bool hasBaked() const noexcept;
};

//--------------------------------------------------------------------------------------------------
class TransformSystem final : public System, public ISerializeSystem
{
	LinearPool<TransformComponent> components;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	~TransformSystem() final;
	void initialize() final;
	void terminate() final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(conf::Writer& writer,
		uint32 index, ID<Component> component) final;
	bool deserialize(conf::Reader& reader,
		uint32 index, ID<Entity> entity) final;
	void postDeserialize(conf::Reader& reader) final;
	
	static void destroyRecursive(Manager* manager, ID<Entity> entity);

	friend class ecsm::Manager;
	friend class TransformEditor;
public:
	const LinearPool<TransformComponent>& getComponents()
		const noexcept { return components; }
	void destroyRecursive(ID<Entity> entity);
};

//--------------------------------------------------------------------------------------------------
struct BakedTransformComponent final : public Component { };

class BakedTransformSystem final : public System
{
protected:
	LinearPool<BakedTransformComponent, false> components;

	type_index getComponentType() const override {
		return typeid(BakedTransformComponent); }
	ID<Component> createComponent(ID<Entity> entity) override {
		return ID<Component>(components.create()); }
	void destroyComponent(ID<Component> instance) override {
		components.destroy(ID<BakedTransformComponent>(instance)); }
	View<Component> getComponent(ID<Component> instance) override {
		return View<Component>(components.get(ID<BakedTransformComponent>(instance))); }
	void disposeComponents() override { components.dispose(); }

	friend class ecsm::Manager;
};

} // garden
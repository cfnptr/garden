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

#include "garden/system/transform.hpp"
#include "garden/system/editor/transform.hpp"

#if GARDEN_DEBUG
#include "garden/system/log.hpp"
#endif

using namespace garden;

//--------------------------------------------------------------------------------------------------
bool TransformComponent::destroy()
{
	GARDEN_ASSERT(entity);

	if (parent)
	{
		auto parentTransform = manager->get<TransformComponent>(parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity) continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransform->childCount--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (childs)
	{
		for (uint32 i = 0; i < childCount; i++)
		{
			auto childTransform = manager->get<TransformComponent>(childs[i]);
			childTransform->parent = {};
		}
		free(childs); // assuming that ID<> has no damagable constructor.
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
float4x4 TransformComponent::calcModel() const noexcept
{
	auto model = ::calcModel(position, rotation, this->scale);

	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransform = manager->get<TransformComponent>(nextParent);
		auto parentModel = ::calcModel(nextTransform->position,
			nextTransform->rotation, nextTransform->scale);
		model = parentModel * model;
		nextParent = nextTransform->parent;
	}
		
	return model;
}
void TransformComponent::setParent(ID<Entity> parent)
{
	GARDEN_ASSERT(parent != entity);
	if (this->parent == parent) return;

	#if GARDEN_DEBUG
	if (parent)
	{
		auto parentTransform = manager->get<TransformComponent>(parent);
		GARDEN_ASSERT(!parentTransform->hasAncestor(entity));
	}
	#endif

	if (this->parent)
	{
		auto parentTransform = manager->get<TransformComponent>(this->parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity) continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransform->childCount--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (parent)
	{
		auto parentTransform = manager->get<TransformComponent>(parent);
		if (parentTransform->childCount == parentTransform->childCapacity)
		{
			if (parentTransform->childs)
			{
				auto newCapacity = parentTransform->childCapacity * 2;
				auto newChilds = (ID<Entity>*)realloc(
					parentTransform->childs, newCapacity * sizeof(ID<Entity>));
				if (!newChilds) abort();
				parentTransform->childs = newChilds;
				parentTransform->childCapacity = newCapacity;
			}
			else
			{
				auto childs = (ID<Entity>*)malloc(sizeof(ID<Entity>));
				if (!childs) abort();
				parentTransform->childs = childs;
				parentTransform->childCapacity = 1;
			}
		}

		parentTransform->childs[parentTransform->childCount++] = entity;
	}

	this->parent = parent;
}

//--------------------------------------------------------------------------------------------------
void TransformComponent::addChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);
	auto childTransform = manager->get<TransformComponent>(child);
	if (childTransform->parent == entity) return;

	#if GARDEN_DEBUG
	auto parentTransform = manager->get<TransformComponent>(parent);
	GARDEN_ASSERT(!parentTransform->hasAncestor(entity));
	#endif

	if (childTransform->parent)
	{
		auto parentTransform = manager->get<TransformComponent>(
			childTransform->parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity) continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransform->childCount--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (childCount == childCapacity)
	{
		if (childs)
		{
			auto newCapacity = childCapacity * 2;
			auto newChilds = (ID<Entity>*)realloc(
				childs, newCapacity * sizeof(ID<Entity>));
			if (!newChilds) abort();
			childs = newChilds;
			childCapacity = newCapacity;
		}
		else
		{
			childs = (ID<Entity>*)malloc(sizeof(ID<Entity>));
			if (!childs) abort();
			childCapacity = 1;
		}
	}

	childs[childCount++] = entity;
	childTransform->parent = entity;
}

//--------------------------------------------------------------------------------------------------
void TransformComponent::removeChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto childTransform = manager->get<TransformComponent>(child);
	for (uint32 i = 0; i < childCount; i++)
	{
		if (childs[i] != entity) continue;
		for (uint32 j = i + 1; j < childCount; j++)
			childs[j - 1] = childs[j];
		childCount--;
		childTransform->parent = {};
		return;
	}

	abort(); // Failed to remove from parent, corrupted memory.
}
void TransformComponent::removeChild(uint32 index)
{
	GARDEN_ASSERT(index < childCount);
	removeChild(childs[index]);
}
void TransformComponent::removeAllChilds()
{
	for (uint32 i = 0; i < childCount; i++)
	{
		auto childTransform = manager->get<TransformComponent>(childs[i]);
		childTransform->parent = {};
	}
	childCount = 0;
}
bool TransformComponent::hasChild(ID<Entity> child) const noexcept
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);
	for (uint32 i = 0; i < childCount; i++)
	{
		if (childs[i] == child) return true;
	}
	return false;
}
bool TransformComponent::hasAncestor(ID<Entity> ancestor) const noexcept
{
	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransform = manager->get<TransformComponent>(nextParent);
		if (ancestor == nextTransform->entity) return true;
		nextParent = nextTransform->parent;
	}
	return false;
}

//--------------------------------------------------------------------------------------------------
static bool hasBakedTransform(Manager* manager, ID<Entity> entity)
{
	if (manager->has<BakedTransformComponent>(entity)) return true;
	auto transformComponent = manager->get<TransformComponent>(entity);
	for (uint32 i = 0; i < transformComponent->getChildCount(); i++)
	{
		if (hasBakedTransform(manager, transformComponent->getChilds()[i])) return true;
	}
	return false;
}
bool TransformComponent::hasBaked() const noexcept
{
	return hasBakedTransform(manager, entity);
}

//--------------------------------------------------------------------------------------------------
TransformSystem::~TransformSystem()
{
	auto componentData = components.getData();
	auto componentOccupancy = components.getOccupancy();
	for (uint32 i = 0; i < componentOccupancy; i++)
		free(componentData[i].childs);
	components.clear(false);
}

//--------------------------------------------------------------------------------------------------
void TransformSystem::initialize()
{
	#if GARDEN_EDITOR
	editor = new TransformEditor(this);
	#endif
}
void TransformSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (TransformEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
type_index TransformSystem::getComponentType() const
{
	return typeid(TransformComponent);
}
ID<Component> TransformSystem::createComponent(ID<Entity> entity)
{
	auto component = components.create();
	auto componentView = components.get(component);
	componentView->entity = entity;
	componentView->manager = getManager();
	#if GARDEN_DEBUG || GARDEN_EDITOR
	componentView->name = "Entity " + to_string(*entity);
	#endif
	return ID<Component>(component);
}
void TransformSystem::destroyComponent(ID<Component> instance)
{
	#if GARDEN_EDITOR
	auto component = components.get(ID<TransformComponent>(instance));
	((TransformEditor*)editor)->onDestroy(component->entity);
	#endif

	components.destroy(ID<TransformComponent>(instance));
}
View<Component> TransformSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<TransformComponent>(instance)));
}
void TransformSystem::disposeComponents() { components.dispose(); }

//--------------------------------------------------------------------------------------------------
void TransformSystem::serialize(conf::Writer& writer,
	uint32 index, ID<Component> component)
{
	auto transformComponent = components.get(ID<TransformComponent>(component));
	auto position = transformComponent->position;
	auto rotation = transformComponent->rotation;
	auto scale = transformComponent->scale;

	auto entityName = "e" + to_string(index);
	writer.write(entityName + ".transform", *transformComponent->entity);

	#if GARDEN_EDITOR
	writer.write(entityName + ".name", transformComponent->name);
	#endif

	if (position.x != 0.0f) writer.write(entityName + ".position.x", position.x, 4);
	if (position.y != 0.0f) writer.write(entityName + ".position.y", position.y, 4);
	if (position.z != 0.0f) writer.write(entityName + ".position.z", position.z, 4);
	if (rotation.x != 0.0f) writer.write(entityName + ".rotation.x", rotation.x, 6);
	if (rotation.y != 0.0f) writer.write(entityName + ".rotation.y", rotation.y, 6);
	if (rotation.z != 0.0f) writer.write(entityName + ".rotation.z", rotation.z, 6);
	if (rotation.w != 1.0f) writer.write(entityName + ".rotation.w", rotation.w, 6);

	if (scale.x == scale.y && scale.x == scale.z)
	{
		if (scale.x != 1.0f) writer.write(entityName + ".scale", scale.x, 6);
	}
	else
	{
		if (scale.x != 1.0f) writer.write(entityName + ".scale.x", scale.x, 6);
		if (scale.y != 1.0f) writer.write(entityName + ".scale.y", scale.y, 6);
		if (scale.z != 1.0f) writer.write(entityName + ".scale.z", scale.z, 6);
	}

	if (transformComponent->parent)
		writer.write(entityName + ".parent", *transformComponent->parent);
}

//--------------------------------------------------------------------------------------------------
static map<uint32, ID<Entity>> entities;
static vector<pair<uint32, ID<Entity>>> needParents;

bool TransformSystem::deserialize(conf::Reader& reader,
	uint32 index, ID<Entity> entity)
{
	auto entityName = "e" + to_string(index); uint32 id = 0;
	if (!reader.get(entityName + ".transform", id)) return false;

	if (!entities.emplace(id, entity).second)
	{
		#if GARDEN_DEBUG
		auto logSystem = getManager()->tryGet<LogSystem>();
		if (logSystem) logSystem->warn("Scene entity with the same ID. (" + to_string(id) + ")");
		#endif
		return false;
	}

	float3 position = float3(0.0), scale = float3(1.0f);
	quat rotation = quat::identity; uint32 parentID = 0;
	
	reader.get(entityName + ".position.x", position.x);
	reader.get(entityName + ".position.y", position.y);
	reader.get(entityName + ".position.z", position.z);
	reader.get(entityName + ".position.x", rotation.x);
	reader.get(entityName + ".rotation.y", rotation.y);
	reader.get(entityName + ".rotation.z", rotation.z);
	reader.get(entityName + ".rotation.w", rotation.w);
	reader.get(entityName + ".scale.x", scale.x);
	reader.get(entityName + ".scale.y", scale.y);
	reader.get(entityName + ".scale.z", scale.z);
	reader.get(entityName + ".parent", parentID);

	if (reader.get(entityName + ".scale", scale.x))
		scale.z = scale.y = scale.x;

	auto transformComponent = getManager()->add<TransformComponent>(entity);
	transformComponent->position = position;
	transformComponent->scale = scale;
	transformComponent->rotation = normalize(rotation);

	#if GARDEN_EDITOR
	string_view name;
	if (reader.get(entityName + ".name", name))
		transformComponent->name = name;
	#endif

	if (parentID > 0)
	{
		auto searchResult = entities.find(parentID);
		if (searchResult == entities.end())
			needParents.emplace_back(make_pair(parentID, entity));
		else transformComponent->setParent(searchResult->second);
	}

	return true;
}
void TransformSystem::postDeserialize(conf::Reader& reader)
{
	auto manager = getManager();
	for (auto& pair : needParents)
	{
		auto searchResult = entities.find(pair.first);
		if (searchResult == entities.end())
		{
			#if GARDEN_DEBUG
			auto logSystem = getManager()->tryGet<LogSystem>();
			if (logSystem)
			{
				logSystem->warn("Missing scene entity parent. (" +
					to_string(pair.first) + ")");
			}
			#endif
			continue;
		}

		auto transformComponent = manager->get<TransformComponent>(pair.second);
		transformComponent->setParent(searchResult->second);
	}

	needParents.clear();
	entities.clear();
}

//--------------------------------------------------------------------------------------------------
void TransformSystem::destroyRecursive(Manager* manager, ID<Entity> entity)
{
	if (manager->has<DoNotDestroyComponent>(entity)) return;
	auto transformComponent = manager->get<TransformComponent>(entity);
	for (uint32 i = 0; i < transformComponent->childCount; i++)
		destroyRecursive(manager, transformComponent->childs[i]);
	transformComponent->childCount = 0;
	transformComponent->parent = {};
	manager->destroy(transformComponent->entity);
}
void TransformSystem::destroyRecursive(ID<Entity> entity)
{
	auto manager = getManager();
	auto transformComponent = manager->get<TransformComponent>(entity);
	for (uint32 i = 0; i < transformComponent->getChildCount(); i++)
		destroyRecursive(manager, transformComponent->childs[i]);
	transformComponent->childCount = 0;
	manager->destroy(transformComponent->entity);
}
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

// TODO: use stacks instead of recursion, potentially unsafe!

#include "garden/system/transform.hpp"
#include "garden/system/editor/transform.hpp"
#include "garden/defines.hpp"

#if GARDEN_DEBUG
#include "garden/system/log.hpp"
#endif

using namespace ecsm;
using namespace garden;

//**********************************************************************************************************************
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
		free(childs); // assuming that ID<> has no damageable constructor.
	}

	return true;
}

//**********************************************************************************************************************
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

//**********************************************************************************************************************
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

//**********************************************************************************************************************
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

//**********************************************************************************************************************
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

//**********************************************************************************************************************
TransformSystem::TransformSystem(Manager* manager) : System(manager)
{
	#if 0
	SUBSCRIBE_TO_EVENT("Init", TransformSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", TransformSystem::deinit);
	#endif
}
TransformSystem::~TransformSystem()
{
	#if 0
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", TransformSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", TransformSystem::deinit);
	}
	#endif

	auto componentData = components.getData();
	auto componentOccupancy = components.getOccupancy();
	for (uint32 i = 0; i < componentOccupancy; i++)
		free(componentData[i].childs);
	components.clear(false);
}

#if 0
void TransformSystem::init()
{
	editor = new TransformEditor(this);
}
void TransformSystem::deinit()
{
	delete (TransformEditor*)editor;
}
#endif

//**********************************************************************************************************************
type_index TransformSystem::getComponentType() const
{
	return typeid(TransformComponent);
}
ID<Component> TransformSystem::createComponent(ID<Entity> entity)
{
	auto component = components.create();
	auto componentView = components.get(component);
	componentView->manager = getManager();
	#if 0 && (GARDEN_DEBUG || GARDEN_EDITOR)
	componentView->name = "Entity " + to_string(*entity);
	#endif
	return ID<Component>(component);
}
void TransformSystem::destroyComponent(ID<Component> instance)
{
	#if 0
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

//**********************************************************************************************************************
void TransformSystem::serialize(ISerializer& serializer, ID<Entity> entity)
{
	auto manager = getManager();
	auto transformComponent = manager->get<TransformComponent>(entity);
	serializer.write("position", transformComponent->position, 4);
	serializer.write("rotation", transformComponent->rotation, 6);
	serializer.write("scale", transformComponent->scale, 6);
}
void TransformSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity)
{
	auto manager = getManager();
	auto transformComponent = manager->get<TransformComponent>(entity);
	deserializer.read("position", transformComponent->position);
	deserializer.read("rotation", transformComponent->rotation);
	deserializer.read("scale", transformComponent->scale);
}

//**********************************************************************************************************************
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
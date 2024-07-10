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

#include "garden/system/transform.hpp"
#include "garden/system/log.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/transform.hpp"
#endif

using namespace ecsm;
using namespace garden;

//**********************************************************************************************************************
bool TransformComponent::destroy()
{
	GARDEN_ASSERT(entity);
	auto manager = Manager::getInstance();

	if (parent)
	{
		auto parentTransform = manager->get<TransformComponent>(parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
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
float4x4 TransformComponent::calcModel(const float3& cameraPosition) const noexcept
{
	auto manager = Manager::getInstance();
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

	setTranslation(model, getTranslation(model) - cameraPosition);
	return model;
}
void TransformComponent::setParent(ID<Entity> parent)
{
	GARDEN_ASSERT(parent != entity);
	if (this->parent == parent)
		return;

	#if GARDEN_DEBUG
	if (parent)
	{
		auto parentTransform = Manager::getInstance()->get<TransformComponent>(parent);
		GARDEN_ASSERT(!parentTransform->hasAncestor(entity));
	}
	#endif

	if (this->parent)
	{
		auto parentTransform = Manager::getInstance()->get<TransformComponent>(this->parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
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
		auto parentTransform = Manager::getInstance()->get<TransformComponent>(parent);
		if (parentTransform->childCount == parentTransform->childCapacity)
		{
			if (parentTransform->childs)
			{
				auto newCapacity = parentTransform->childCapacity * 2;
				auto newChilds = realloc<ID<Entity>>(parentTransform->childs, newCapacity);
				parentTransform->childs = newChilds;
				parentTransform->childCapacity = newCapacity;
			}
			else
			{
				auto childs = malloc<ID<Entity>>(1);
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
	auto manager = Manager::getInstance();

	auto childTransform = manager->get<TransformComponent>(child);
	if (childTransform->parent == entity)
		return;

	#if GARDEN_DEBUG
	auto parentTransform = manager->get<TransformComponent>(parent);
	GARDEN_ASSERT(!parentTransform->hasAncestor(entity));
	#endif

	if (childTransform->parent)
	{
		auto parentTransform = manager->get<TransformComponent>(childTransform->parent);
		auto parentChildCount = parentTransform->childCount;
		auto parentChilds = parentTransform->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
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
			auto newChilds = realloc<ID<Entity>>(childs, newCapacity);
			childs = newChilds;
			childCapacity = newCapacity;
		}
		else
		{
			childs = malloc<ID<Entity>>(1);
			childCapacity = 1;
		}
	}

	childs[childCount++] = entity;
	childTransform->parent = entity;
}

bool TransformComponent::hasChild(ID<Entity> child) const noexcept
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);
	for (uint32 i = 0; i < childCount; i++)
	{
		if (childs[i] == child)
			return true;
	}
	return false;
}

//**********************************************************************************************************************
void TransformComponent::removeChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto childTransform = Manager::getInstance()->get<TransformComponent>(child);
	for (uint32 i = 0; i < childCount; i++)
	{
		if (childs[i] != entity)
			continue;
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
	auto manager = Manager::getInstance();
	for (uint32 i = 0; i < childCount; i++)
	{
		auto childTransform = manager->get<TransformComponent>(childs[i]);
		childTransform->parent = {};
	}
	childCount = 0;
}

//**********************************************************************************************************************
bool TransformComponent::hasAncestor(ID<Entity> ancestor) const noexcept
{
	auto manager = Manager::getInstance();
	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransform = manager->get<TransformComponent>(nextParent);
		if (ancestor == nextTransform->entity)
			return true;
		nextParent = nextTransform->parent;
	}
	return false;
}

bool TransformComponent::isActiveWithAncestors() const noexcept
{
	if (!isActive)
		return false;

	auto manager = Manager::getInstance();
	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransform = manager->get<TransformComponent>(nextParent);
		if (!nextTransform->isActive)
			return false;
		nextParent = nextTransform->parent;
	}
	return true;
}

//**********************************************************************************************************************
bool TransformComponent::hasBakedWithDescendants() const noexcept
{
	static vector<ID<Entity>> transformStack;
	transformStack.push_back(entity);

	auto manager = Manager::getInstance();
	while (!transformStack.empty())
	{
		auto transform = transformStack.back();
		transformStack.pop_back();

		if (manager->has<BakedTransformComponent>(transform))
		{
			transformStack.clear();
			return true;
		}

		auto transformComponent = manager->get<TransformComponent>(transform);
		auto childCount = transformComponent->getChildCount();
		auto childs = transformComponent->getChilds();
		
		for (uint32 i = 0; i < childCount; i++)
			transformStack.push_back(childs[i]);
	}

	return false;
}

//**********************************************************************************************************************
TransformSystem* TransformSystem::instance = nullptr;

TransformSystem::TransformSystem()
{
	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
TransformSystem::~TransformSystem()
{
	auto componentData = components.getData();
	auto componentOccupancy = components.getOccupancy();
	for (uint32 i = 0; i < componentOccupancy; i++)
		free(componentData[i].childs);
	components.clear(false);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
ID<Component> TransformSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void TransformSystem::destroyComponent(ID<Component> instance)
{
	#if GARDEN_EDITOR
	auto component = components.get(ID<TransformComponent>(instance));
	TransformEditorSystem::getInstance()->onEntityDestroy(component->entity);
	#endif
	components.destroy(ID<TransformComponent>(instance));
}
void TransformSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<TransformComponent>(source);
	auto destinationView = View<TransformComponent>(destination);
	destinationView->position = sourceView->position;
	destinationView->scale = sourceView->scale;
	destinationView->rotation = sourceView->rotation;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->debugName = sourceView->debugName;
	#endif
}
const string& TransformSystem::getComponentName() const
{
	static const string name = "Transform";
	return name;
}
type_index TransformSystem::getComponentType() const
{
	return typeid(TransformComponent);
}
View<Component> TransformSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<TransformComponent>(instance)));
}
void TransformSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

//**********************************************************************************************************************
void TransformSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<TransformComponent>(component);
	serializer.write("uid", *componentView->entity);

	if (componentView->position != float3(0.0f))
		serializer.write("position", componentView->position);
	if (componentView->rotation != quat::identity)
		serializer.write("rotation", componentView->rotation);
	if (componentView->scale != float3(1.0f))
		serializer.write("scale", componentView->scale);

	auto manager = Manager::getInstance();
	auto parent = componentView->parent;
	if (parent && !manager->has<DoNotSerializeComponent>(parent))
		serializer.write("parent", *parent);

	#if GARDEN_DEBUG | GARDEN_EDITOR
	if (!componentView->debugName.empty())
		serializer.write("debugName", componentView->debugName);
	#endif
}
void TransformSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<TransformComponent>(component);

	uint32 uid = 0;
	deserializer.read("uid", uid);

	auto result = deserializeEntities.emplace(uid, entity);
	if (!result.second)
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Deserialized entity with already existing uid. (uid: " + to_string(uid) + ")");
	}
	
	deserializer.read("position", componentView->position);
	deserializer.read("rotation", componentView->rotation);
	deserializer.read("scale", componentView->scale);

	uint32 parent = 0;
	if (deserializer.read("parent", parent))
		deserializeParents.emplace_back(make_pair(entity, parent));

	#if GARDEN_DEBUG | GARDEN_EDITOR
	deserializer.read("debugName", componentView->debugName);
	#endif
}
void TransformSystem::postDeserialize(IDeserializer& deserializer)
{
	auto manager = Manager::getInstance();
	for (auto pair : deserializeParents)
	{
		auto parent = deserializeEntities.find(pair.second);
		if (parent == deserializeEntities.end())
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
			{
				logSystem->error("Deserialized entity parent does not exist. ("
					"parentUID: " + to_string(pair.second) + ")");
			}
		}

		auto transformComponent = manager->get<TransformComponent>(pair.first);
		transformComponent->setParent(parent->second);
	}

	deserializeParents.clear();
	deserializeEntities.clear();
}

//**********************************************************************************************************************
void TransformSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	auto frameView = View<TransformFrame>(frame);
	if (frameView->animatePosition)
		serializer.write("position", frameView->position);
	if (frameView->animateScale)
		serializer.write("scale", frameView->scale);
	if (frameView->animateRotation)
		serializer.write("rotation", frameView->rotation);
}
ID<AnimationFrame> TransformSystem::deserializeAnimation(IDeserializer& deserializer)
{
	TransformFrame frame;
	frame.animatePosition = deserializer.read("position", frame.position);
	frame.animateScale = deserializer.read("scale", frame.scale);
	frame.animateRotation = deserializer.read("rotation", frame.rotation);
	
	if (frame.animatePosition || frame.animateScale || frame.animateRotation)
	{
		auto instance = animationFrames.create();
		auto frameView = animationFrames.get(instance);
		**frameView = frame;
		return ID<AnimationFrame>(instance);
	}

	return {};
}
View<AnimationFrame> TransformSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<TransformFrame>(frame)));
}

//**********************************************************************************************************************
void TransformSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<TransformComponent>(component);
	auto frameA = View<TransformFrame>(a);
	auto frameB = View<TransformFrame>(b);

	if (frameA->animatePosition)
		componentView->position = lerp(frameA->position, frameB->position, t);
	if (frameA->animateScale)
		componentView->scale = lerp(frameA->scale, frameB->scale, t);
	if (frameA->animateRotation)
		componentView->rotation = slerp(frameA->rotation, frameB->rotation, t);
}
void TransformSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<TransformFrame>(frame));
}

//**********************************************************************************************************************
void TransformSystem::destroyRecursive(ID<Entity> entity)
{
	auto manager = Manager::getInstance();
	if (manager->has<DoNotDestroyComponent>(entity))
		return;

	auto transformComponent = manager->get<TransformComponent>(entity);
	auto childCount = transformComponent->childCount;
	auto childs = transformComponent->childs;
	transformComponent->childCount = 0;

	for (uint32 i = 0; i < childCount; i++)
		transformStack.push_back(childs[i]);

	while (!transformStack.empty())
	{
		auto transform = transformStack.back();
		transformStack.pop_back();

		if (manager->has<DoNotDestroyComponent>(transform))
			continue;

		transformComponent = manager->get<TransformComponent>(transform);
		childCount = transformComponent->childCount;
		childs = transformComponent->childs;

		for (uint32 i = 0; i < childCount; i++)
			transformStack.push_back(childs[i]);

		transformComponent->childCount = 0;
		transformComponent->parent = {};
		manager->destroy(transform);
	}
	
	manager->destroy(entity);
}
ID<Entity> TransformSystem::duplicateRecursive(ID<Entity> entity)
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(!manager->has<DoNotDuplicateComponent>(entity));

	auto entityDuplicate = manager->duplicate(entity);
	auto entityComponent = manager->get<TransformComponent>(entity);
	auto duplicateComponent = manager->get<TransformComponent>(entityDuplicate);
	duplicateComponent->setParent(entityComponent->getParent());
	entityDuplicateStack.emplace_back(entity, entityDuplicate);

	while (!entityDuplicateStack.empty())
	{
		auto pair = entityDuplicateStack.back();
		entityDuplicateStack.pop_back();

		if (manager->has<DoNotDuplicateComponent>(pair.first))
			continue;

		entityComponent = manager->get<TransformComponent>(pair.first);
		auto childCount = entityComponent->childCount;
		auto childs = entityComponent->childs;

		for (uint32 i = 0; i < childCount; i++)
		{
			auto child = childs[i];
			auto duplicate = manager->duplicate(child);
			duplicateComponent = manager->get<TransformComponent>(duplicate);
			duplicateComponent->setParent(pair.second);
			entityDuplicateStack.emplace_back(child, duplicate);
		}
	}

	return entityDuplicate;
}

//**********************************************************************************************************************
ID<Component> BakedTransformSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void BakedTransformSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<BakedTransformComponent>(instance));
}
void BakedTransformSystem::copyComponent(View<Component> source, View<Component> destination)
{
	return;
}
const string& BakedTransformSystem::getComponentName() const
{
	static const string name = "Baked Transform";
	return name;
}
type_index BakedTransformSystem::getComponentType() const
{
	return typeid(BakedTransformComponent);
}
View<Component> BakedTransformSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<BakedTransformComponent>(instance)));
}
void BakedTransformSystem::disposeComponents()
{
	components.dispose();
}

void BakedTransformSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	return;
}
void BakedTransformSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	return;
}
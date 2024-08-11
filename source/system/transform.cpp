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
#include "garden/system/physics.hpp"
#include "garden/system/log.hpp"
#include "garden/base64.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/transform.hpp"
#endif

using namespace ecsm;
using namespace garden;

//**********************************************************************************************************************
bool TransformComponent::destroy()
{
	if (parent)
	{
		auto parentTransformView = TransformSystem::get()->get(parent);
		auto parentChildCount = parentTransformView->childCount;
		auto parentChilds = parentTransformView->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];

			parentTransformView->childCount--;
			parent = {};
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (childs)
	{
		auto transformSystem = TransformSystem::get();
		for (uint32 i = 0; i < childCount; i++)
		{
			auto childTransformView = transformSystem->get(childs[i]);
			childTransformView->parent = {};
		}

		free(childs); // assuming that ID<> has no damageable constructor.
		childs = nullptr;
	}

	return true;
}

//**********************************************************************************************************************
float4x4 TransformComponent::calcModel(const float3& cameraPosition) const noexcept
{
	auto transformSystem = TransformSystem::get();
	auto model = ::calcModel(position, rotation, this->scale);

	// TODO: rethink this architecture, we should't access physics system from here :(
	auto rigidbodyComponent = Manager::get()->tryGet<RigidbodyComponent>(entity);
	if (rigidbodyComponent && rigidbodyComponent->getMotionType() != MotionType::Static &&
		!Manager::get()->has<CharacterComponent>(entity))
	{
		setTranslation(model, position - cameraPosition);
		return model;
	}

	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransformView = transformSystem->get(nextParent);
		auto parentModel = ::calcModel(nextTransformView->position,
			nextTransformView->rotation, nextTransformView->scale);
		model = parentModel * model;
		nextParent = nextTransformView->parent;
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
		auto parentTransformView = TransformSystem::get()->get(parent);
		GARDEN_ASSERT(!parentTransformView->hasAncestor(entity));
	}
	#endif

	if (this->parent)
	{
		auto parentTransformView = TransformSystem::get()->get(this->parent);
		auto parentChildCount = parentTransformView->childCount;
		auto parentChilds = parentTransformView->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransformView->childCount--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (parent)
	{
		auto parentTransformView = TransformSystem::get()->get(parent);
		if (parentTransformView->childCount == parentTransformView->childCapacity)
		{
			if (parentTransformView->childs)
			{
				auto newCapacity = parentTransformView->childCapacity * 2;
				auto newChilds = realloc<ID<Entity>>(parentTransformView->childs, newCapacity);
				parentTransformView->childs = newChilds;
				parentTransformView->childCapacity = newCapacity;
			}
			else
			{
				auto childs = malloc<ID<Entity>>(1);
				parentTransformView->childs = childs;
				parentTransformView->childCapacity = 1;
			}
		}

		parentTransformView->childs[parentTransformView->childCount++] = entity;
	}

	this->parent = parent;
}

//**********************************************************************************************************************
void TransformComponent::addChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);
	auto transformSystem = TransformSystem::get();

	auto childTransformView = transformSystem->get(child);
	if (childTransformView->parent == entity)
		return;

	#if GARDEN_DEBUG
	if (parent)
	{
		auto parentTransformView = transformSystem->get(parent);
		GARDEN_ASSERT(!parentTransformView->hasAncestor(entity));
	}
	#endif

	if (childTransformView->parent)
	{
		auto parentTransformView = transformSystem->get(childTransformView->parent);
		auto parentChildCount = parentTransformView->childCount;
		auto parentChilds = parentTransformView->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransformView->childCount--;
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
	childTransformView->parent = entity;
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

	auto childTransformView = TransformSystem::get()->get(child);
	for (uint32 i = 0; i < childCount; i++)
	{
		if (childs[i] != entity)
			continue;
		for (uint32 j = i + 1; j < childCount; j++)
			childs[j - 1] = childs[j];
		childCount--;
		childTransformView->parent = {};
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
	auto transformSystem = TransformSystem::get();
	for (uint32 i = 0; i < childCount; i++)
	{
		auto childTransformView = transformSystem->get(childs[i]);
		childTransformView->parent = {};
	}
	childCount = 0;
}
void TransformComponent::shrinkChilds()
{
	if (!childs)
		return;

	auto newChilds = realloc<ID<Entity>>(childs, childCount);
	childs = newChilds;
}

//**********************************************************************************************************************
bool TransformComponent::hasAncestor(ID<Entity> ancestor) const noexcept
{
	auto transformSystem = TransformSystem::get();
	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransformView = transformSystem->get(nextParent);
		if (ancestor == nextTransformView->entity)
			return true;
		nextParent = nextTransformView->parent;
	}
	return false;
}

bool TransformComponent::isActiveWithAncestors() const noexcept
{
	if (!isActive)
		return false;

	auto transformSystem = TransformSystem::get();
	auto nextParent = parent;
	while (nextParent)
	{
		auto nextTransformView = transformSystem->get(nextParent);
		if (!nextTransformView->isActive)
			return false;
		nextParent = nextTransformView->parent;
	}
	return true;
}

//**********************************************************************************************************************
bool TransformComponent::hasBakedWithDescendants() const noexcept
{
	static thread_local vector<ID<Entity>> entityStack;
	entityStack.push_back(entity);

	auto manager = Manager::get();
	auto transformSystem = TransformSystem::get();

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		if (manager->has<BakedTransformComponent>(entity))
		{
			entityStack.clear();
			return true;
		}

		auto transformView = transformSystem->get(entity);
		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();
		
		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
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
	auto componentView = components.get(ID<TransformComponent>(instance));
	TransformEditorSystem::get()->onEntityDestroy(componentView->entity);
	#endif
	components.destroy(ID<TransformComponent>(instance));
}
void TransformSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<TransformComponent>(source);
	auto destinationView = View<TransformComponent>(destination);
	destinationView->destroy();

	destinationView->position = sourceView->position;
	destinationView->scale = sourceView->scale;
	destinationView->rotation = sourceView->rotation;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->debugName = sourceView->debugName;
	#endif
	destinationView->isActive = sourceView->isActive;
	destinationView->uid = 0;
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
void TransformSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<TransformComponent>(component);

	if (!componentView->uid)
	{
		auto& randomDevice = serializer.randomDevice;
		uint32 uid[2] { randomDevice(), randomDevice() };
		componentView->uid = *(uint64*)(uid);
		GARDEN_ASSERT(componentView->uid); // Something is wrong with the random device.
	}

	#if GARDEN_DEBUG
	auto emplaceResult = serializedEntities.emplace(componentView->uid);
	GARDEN_ASSERT(emplaceResult.second); // Detected several entities with the same UID.
	#endif

	encodeBase64(uidStringCache, &componentView->uid, sizeof(uint64));
	uidStringCache.resize(uidStringCache.length() - 1);
	serializer.write("uid", uidStringCache);

	if (componentView->position != float3(0.0f))
		serializer.write("position", componentView->position);
	if (componentView->rotation != quat::identity)
		serializer.write("rotation", componentView->rotation);
	if (componentView->scale != float3(1.0f))
		serializer.write("scale", componentView->scale);
	if (!componentView->isActive)
		serializer.write("isActive", false);

	if (componentView->parent)
	{
		auto parentView = get(componentView->parent);
		if (!parentView->uid)
		{
			auto& randomDevice = serializer.randomDevice;
			uint32 uid[2]{ randomDevice(), randomDevice() };
			parentView->uid = *(uint64*)(uid);
			GARDEN_ASSERT(parentView->uid); // Something is wrong with the random device.
		}
		encodeBase64(uidStringCache, &parentView->uid, sizeof(uint64));
		uidStringCache.resize(uidStringCache.length() - 1);
		serializer.write("parent", uidStringCache);
	}

	#if GARDEN_DEBUG | GARDEN_EDITOR
	if (!componentView->debugName.empty())
		serializer.write("debugName", componentView->debugName);
	#endif
}
void TransformSystem::postSerialize(ISerializer& serializer)
{
	#if GARDEN_DEBUG
	serializedEntities.clear();
	#endif
}

void TransformSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<TransformComponent>(component);

	if (deserializer.read("uid", uidStringCache) &&
		uidStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)))
	{
		if (decodeBase64(&componentView->uid, uidStringCache, ModpDecodePolicy::kForgiving))
		{
			auto result = deserializedEntities.emplace(componentView->uid, entity);
			if (!result.second)
			{
				auto logSystem = Manager::get()->tryGet<LogSystem>();
				if (logSystem)
					logSystem->error("Deserialized entity with already existing UID. (uid: " + uidStringCache + ")");
			}
		}
	}

	deserializer.read("position", componentView->position);
	deserializer.read("rotation", componentView->rotation);
	deserializer.read("scale", componentView->scale);
	deserializer.read("isActive", componentView->isActive);

	if (deserializer.read("parent", uidStringCache) &&
		uidStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)))
	{
		uint64 parentUID = 0;
		if (decodeBase64(&parentUID, uidStringCache, ModpDecodePolicy::kForgiving))
		{
			if (componentView->uid == parentUID)
			{
				auto logSystem = Manager::get()->tryGet<LogSystem>();
				if (logSystem)
					logSystem->error("Deserialized entity with the same parent UID. (uid: " + uidStringCache + ")");
			}
			else
			{
				deserializedParents.emplace_back(make_pair(entity, parentUID));
			}
			// TODO: maybe also detect recursive descendant parent?
		}
	}

	#if GARDEN_DEBUG | GARDEN_EDITOR
	deserializer.read("debugName", componentView->debugName);
	#endif
}
void TransformSystem::postDeserialize(IDeserializer& deserializer)
{
	for (auto pair : deserializedParents)
	{
		auto parent = deserializedEntities.find(pair.second);
		if (parent == deserializedEntities.end())
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
			{
				encodeBase64(uidStringCache, &pair.second, sizeof(uint64));
				uidStringCache.resize(uidStringCache.length() - 1);
				logSystem->error("Deserialized entity parent does not exist. (parentUID: " + uidStringCache + ")");
			}
			continue;
		}

		auto transformView = get(pair.first);
		transformView->setParent(parent->second);
	}

	deserializedParents.clear();
	deserializedEntities.clear();
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
		return ID<AnimationFrame>(animationFrames.create(frame));
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
	if (!entity)
		return;

	auto manager = Manager::get();
	if (manager->has<DoNotDestroyComponent>(entity))
		return;

	auto transformView = tryGet(entity);
	if (!transformView)
	{
		manager->destroy(entity);
		return;
	}

	auto childCount = transformView->childCount;
	auto childs = transformView->childs;
	transformView->isActive = false;
	transformView->childCount = 0;

	for (uint32 i = 0; i < childCount; i++)
		entityStack.push_back(childs[i]);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		if (manager->has<DoNotDestroyComponent>(entity))
			continue;

		transformView = get(entity);
		childCount = transformView->childCount;
		childs = transformView->childs;

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);

		transformView->isActive = false;
		transformView->childCount = 0;
		transformView->parent = {};
		manager->destroy(entity);
	}
	
	manager->destroy(entity);
}
ID<Entity> TransformSystem::duplicateRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);

	auto manager = Manager::get();
	GARDEN_ASSERT(!manager->has<DoNotDuplicateComponent>(entity));

	auto entityDuplicate = manager->duplicate(entity);

	auto entityTransformView = tryGet(entity);
	if (!entityTransformView)
		return entityDuplicate;

	auto duplicateTransformView = get(entityDuplicate);
	duplicateTransformView->setParent(entityTransformView->getParent());
	entityDuplicateStack.emplace_back(entity, entityDuplicate);

	while (!entityDuplicateStack.empty())
	{
		auto pair = entityDuplicateStack.back();
		entityDuplicateStack.pop_back();

		if (manager->has<DoNotDuplicateComponent>(pair.first))
			continue;

		entityTransformView = get(pair.first);
		auto childCount = entityTransformView->childCount;
		auto childs = entityTransformView->childs;

		for (uint32 i = 0; i < childCount; i++)
		{
			auto child = childs[i];
			auto duplicate = manager->duplicate(child);
			duplicateTransformView = get(duplicate);
			duplicateTransformView->setParent(pair.second);
			entityDuplicateStack.emplace_back(child, duplicate);
		}
	}

	return entityDuplicate;
}

//**********************************************************************************************************************
bool TransformSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(TransformComponent)) != entityComponents.end();
}
View<TransformComponent> TransformSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(TransformComponent));
	return components.get(ID<TransformComponent>(pair.second));
}
View<TransformComponent> TransformSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(TransformComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<TransformComponent>(result->second.second));
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

void BakedTransformSystem::serialize(ISerializer& serializer, const View<Component> componentView)
{
	return;
}
void BakedTransformSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> componentView)
{
	return;
}
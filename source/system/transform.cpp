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

#include "garden/system/transform.hpp"
#include "garden/system/ui/transform.hpp"
#include "garden/system/log.hpp"
#include "garden/base64.hpp"
#include "math/matrix/transform.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/transform.hpp"
#endif

using namespace ecsm;
using namespace garden;

static thread_local vector<ID<Entity>> entityStack;

//**********************************************************************************************************************
bool TransformComponent::destroy()
{
	if (!entity)
		return false;

	if (parent)
	{
		auto parentTransformView = TransformSystem::Instance::get()->getComponent(parent);
		auto parentChildCount = parentTransformView->childCount();
		auto parentChilds = parentTransformView->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];

			parentTransformView->childCount()--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (childs)
	{
		auto transformSystem = TransformSystem::Instance::get();
		auto thisChildCount = childCount();

		for (uint32 i = 0; i < thisChildCount; i++)
		{
			auto childTransformView = transformSystem->getComponent(childs[i]);
			childTransformView->parent = {};
			childTransformView->ancestorsActive = true;
		}

		free(childs); // Warning: assuming that ID<> has no damageable constructor!
	}

	return true;
}

void TransformComponent::setActive(bool isActive) noexcept
{
	if (selfActive == isActive)
		return;

	selfActive = isActive; // Note: Do not move this setter!

	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	if (isActive)
	{
		if (!ancestorsActive)
			return;

		while (!entityStack.empty())
		{
			auto entity = entityStack.back();
			entityStack.pop_back();

			auto transformView = transformSystem->getComponent(entity);
			if (!transformView->selfActive)
				continue;

			auto childCount = transformView->childCount();
			auto childs = transformView->childs;

			for (uint32 i = 0; i < childCount; i++)
			{
				auto child = childs[i];
				auto childTransformView = transformSystem->getComponent(child);
				childTransformView->ancestorsActive = true;
				entityStack.push_back(child);
			}
		}
	}
	else
	{
		while (!entityStack.empty())
		{
			auto entity = entityStack.back();
			entityStack.pop_back();

			auto transformView = transformSystem->getComponent(entity);
			auto childCount = transformView->childCount();
			auto childs = transformView->childs;

			for (uint32 i = 0; i < childCount; i++)
			{
				auto child = childs[i];
				auto childTransformView = transformSystem->getComponent(child);
				childTransformView->ancestorsActive = false;
				entityStack.push_back(child);
			}
		}
	}
}

//**********************************************************************************************************************
f32x4x4 TransformComponent::calcModel(f32x4 cameraPosition) const noexcept
{
	auto transformSystem = TransformSystem::Instance::get();
	auto model = ::calcModel(posChildCount, rotation, scaleChildCap);

	if (modelWithAncestors)
	{
		auto nextParent = parent;
		while (nextParent)
		{
			auto nextTransformView = transformSystem->getComponent(nextParent);
			auto parentModel = ::calcModel(nextTransformView->posChildCount,
				nextTransformView->rotation, nextTransformView->scaleChildCap);
			model = parentModel * model;
			nextParent = nextTransformView->parent;
		}

		return ::translate(-cameraPosition, model);
	}

	setTranslation(model, posChildCount - cameraPosition);
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
		auto parentTransformView = TransformSystem::Instance::get()->getComponent(parent);
		GARDEN_ASSERT(!parentTransformView->hasAncestor(entity));
	}
	#endif

	if (this->parent)
	{
		auto parentTransformView = TransformSystem::Instance::get()->getComponent(this->parent);
		auto parentChildCount = parentTransformView->childCount();
		auto parentChilds = parentTransformView->childs;

		for (uint32 i = 0; i < parentChildCount; i++)
		{
			if (parentChilds[i] != entity)
				continue;
			for (uint32 j = i + 1; j < parentChildCount; j++)
				parentChilds[j - 1] = parentChilds[j];
			parentTransformView->childCount()--;
			goto REMOVED_FROM_PARENT;
		}

		abort(); // Failed to remove from parent, corrupted memory.
	}

REMOVED_FROM_PARENT:

	if (parent)
	{
		auto parentTransformView = TransformSystem::Instance::get()->getComponent(parent);
		if (parentTransformView->childCount() == parentTransformView->childCapacity())
		{
			if (parentTransformView->childs)
			{
				auto newCapacity = parentTransformView->childCapacity() * 2;
				auto newChilds = realloc<ID<Entity>>(parentTransformView->childs, newCapacity);
				parentTransformView->childs = newChilds;
				parentTransformView->childCapacity() = newCapacity;
			}
			else
			{
				auto childs = malloc<ID<Entity>>(1);
				parentTransformView->childs = childs;
				parentTransformView->childCapacity() = 1;
			}
		}

		parentTransformView->childs[parentTransformView->childCount()++] = entity;
		ancestorsActive = parentTransformView->selfActive && parentTransformView->ancestorsActive;
	}
	else
	{
		ancestorsActive = true;
	}

	this->parent = parent;
}

//**********************************************************************************************************************
void TransformComponent::addChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	if (!tryAddChild(child))
		throw GardenError("Failed to add child, child already has a parent.");
}
bool TransformComponent::tryAddChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);
	auto transformSystem = TransformSystem::Instance::get();

	auto childTransformView = transformSystem->getComponent(child);
	if (childTransformView->parent)
		return false;

	GARDEN_ASSERT(!hasAncestor(child));
	if (childCount() == childCapacity())
	{
		if (childs)
		{
			auto newCapacity = childCapacity() * 2;
			auto newChilds = realloc<ID<Entity>>(childs, newCapacity);
			childs = newChilds;
			childCapacity() = newCapacity;
		}
		else
		{
			childs = malloc<ID<Entity>>(1);
			childCapacity() = 1;
		}
	}

	childs[childCount()++] = entity;
	childTransformView->parent = entity;
	childTransformView->ancestorsActive = selfActive && ancestorsActive;
	return true;
}

bool TransformComponent::hasChild(ID<Entity> child) const noexcept
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto thisChildCount = getChildCount();
	for (uint32 i = 0; i < thisChildCount; i++)
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

	if (!tryRemoveChild(child))
		throw GardenError("Failed to remove child, not found.");
}
bool TransformComponent::tryRemoveChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto thisChildCount = childCount();
	for (uint32 i = 0; i < thisChildCount; i++)
	{
		if (childs[i] != entity)
			continue;
		for (uint32 j = i + 1; j < thisChildCount; j++)
			childs[j - 1] = childs[j];

		auto childTransformView = TransformSystem::Instance::get()->getComponent(child);
		childTransformView->parent = {};
		childTransformView->ancestorsActive = true;

		childCount()--;
		return true;
	}

	return false;
}
void TransformComponent::removeAllChilds()
{
	auto transformSystem = TransformSystem::Instance::get();
	auto thisChildCount = childCount();

	for (uint32 i = 0; i < thisChildCount; i++)
	{
		auto childTransformView = transformSystem->getComponent(childs[i]);
		childTransformView->parent = {};
		childTransformView->ancestorsActive = true;
	}

	childCount() = 0;
}
void TransformComponent::shrinkChilds()
{
	if (!childs)
		return;

	if (childCount() > 0)
	{
		auto newChilds = realloc<ID<Entity>>(childs, childCount());
		childs = newChilds;
	}
	else
	{
		free(childs);
		childs = nullptr;
	}
}

//**********************************************************************************************************************
bool TransformComponent::hasAncestor(ID<Entity> ancestor) const noexcept
{
	GARDEN_ASSERT(ancestor);
	GARDEN_ASSERT(ancestor != entity);

	auto transformSystem = TransformSystem::Instance::get();
	auto nextParent = parent;

	while (nextParent)
	{
		auto nextTransformView = transformSystem->getComponent(nextParent);
		if (ancestor == nextTransformView->entity)
			return true;
		nextParent = nextTransformView->parent;
	}
	return false;
}
bool TransformComponent::hasDescendant(ID<Entity> descendant) const noexcept
{
	GARDEN_ASSERT(descendant);
	GARDEN_ASSERT(descendant != entity);

	if (!childs)
		return false;

	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto transformView = transformSystem->getComponent(entity);
		auto transformChildCount = transformView->childCount();
		auto transformChilds = transformView->childs;
		
		for (uint32 i = 0; i < transformChildCount; i++)
		{
			if (transformChilds[i] == descendant)
			{
				entityStack.clear();
				return true;
			}
			entityStack.push_back(transformChilds[i]);
		}
	}

	return false;
}

//**********************************************************************************************************************
bool TransformComponent::hasStaticWithDescendants() const noexcept
{
	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		if (manager->has<StaticTransformComponent>(entity))
		{
			entityStack.clear();
			return true;
		}

		auto transformView = transformSystem->getComponent(entity);
		auto transformChildCount = transformView->childCount();
		auto transformChilds = transformView->getChilds();
		
		for (uint32 i = 0; i < transformChildCount; i++)
			entityStack.push_back(transformChilds[i]);
	}

	return false;
}

//**********************************************************************************************************************
TransformSystem::TransformSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
}

TransformSystem::~TransformSystem()
{
	auto manager = Manager::Instance::get();
	components.clear(manager->isRunning);

	if (manager->isRunning)
	{
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void TransformSystem::destroyComponent(ID<Component> instance)
{
	#if GARDEN_EDITOR
	auto componentView = components.get(ID<TransformComponent>(instance));
	TransformEditorSystem::Instance::get()->onEntityDestroy(componentView->entity);
	#endif
	components.destroy(ID<TransformComponent>(instance));
}
void TransformSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<TransformComponent>(component);
	componentView->setParent({});
	componentView->removeAllChilds();
	componentView->shrinkChilds();

	if (full)
		**componentView = {};
}
void TransformSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<TransformComponent>(source);
	auto destinationView = View<TransformComponent>(destination);
	destinationView->posChildCount = sourceView->posChildCount;
	destinationView->childCount() = 0;
	destinationView->scaleChildCap = sourceView->scaleChildCap;
	destinationView->childCapacity() = 0;
	destinationView->rotation = sourceView->rotation;
	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->debugName = sourceView->debugName;
	#endif
	destinationView->uid = 0;
	destinationView->selfActive = sourceView->selfActive;
}
string_view TransformSystem::getComponentName() const
{
	return "Transform";
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
		GARDEN_ASSERT_MSG(componentView->uid, "Detected random device anomaly");
	}

	#if GARDEN_DEBUG
	auto emplaceResult = serializedEntities.emplace(componentView->uid);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected several entities with the same UID");
	#endif

	encodeBase64URL(uidStringCache, &componentView->uid, sizeof(uint64));
	uidStringCache.resize(uidStringCache.length() - 1);
	serializer.write("uid", uidStringCache);

	auto manager = Manager::Instance::get();
	if (!manager->has<UiTransformComponent>(component->getEntity()))
	{
		if (f32x4(componentView->posChildCount, 0.0f) != f32x4::zero)
			serializer.write("position", (float3)componentView->posChildCount);
		if (componentView->rotation != quat::identity)
			serializer.write("rotation", componentView->rotation);
		if (f32x4(componentView->scaleChildCap, 1.0f) != f32x4::one)
			serializer.write("scale", (float3)componentView->scaleChildCap);
	}
	
	if (!componentView->selfActive)
		serializer.write("isActive", false);

	if (componentView->parent)
	{
		auto parentView = getComponent(componentView->parent);
		if (!parentView->uid)
		{
			auto& randomDevice = serializer.randomDevice;
			uint32 uid[2]{ randomDevice(), randomDevice() };
			parentView->uid = *(uint64*)(uid);
			GARDEN_ASSERT_MSG(parentView->uid, "Detected random device anomaly");
		}
		encodeBase64URL(uidStringCache, &parentView->uid, sizeof(uint64));
		uidStringCache.resize(uidStringCache.length() - 1);
		serializer.write("parent", uidStringCache);
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
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

void TransformSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<TransformComponent>(component);

	if (deserializer.read("uid", uidStringCache) &&
		uidStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)))
	{
		if (decodeBase64URL(&componentView->uid, uidStringCache, ModpDecodePolicy::kForgiving))
		{
			auto result = deserializedEntities.emplace(componentView->uid, componentView->entity);
			if (!result.second)
				GARDEN_LOG_ERROR("Deserialized entity with already existing UID. (uid: " + uidStringCache + ")");
		}
	}

	auto f32x4Value = f32x4::zero; auto rotation = quat::identity;
	deserializer.read("position", f32x4Value, 3);
	componentView->setPosition(f32x4Value);
	deserializer.read("rotation", rotation);
	componentView->setRotation(rotation);
	f32x4Value = f32x4::one;
	deserializer.read("scale", f32x4Value, 3);
	componentView->setScale(f32x4Value);
	deserializer.read("isActive", componentView->selfActive);

	if (deserializer.read("parent", uidStringCache) &&
		uidStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)))
	{
		uint64 parentUID = 0;
		if (decodeBase64URL(&parentUID, uidStringCache, ModpDecodePolicy::kForgiving))
		{
			if (componentView->uid == parentUID)
				GARDEN_LOG_ERROR("Deserialized entity with the same parent UID. (uid: " + uidStringCache + ")");
			else
				deserializedParents.emplace_back(make_pair(componentView->entity, parentUID));
		}
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
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
			encodeBase64URL(uidStringCache, &pair.second, sizeof(uint64));
			uidStringCache.resize(uidStringCache.length() - 1);
			GARDEN_LOG_ERROR("Deserialized entity parent does not exist. (parentUID: " + uidStringCache + ")");
			continue;
		}

		auto transformView = getComponent(pair.first);
		transformView->setParent(parent->second);
	}

	deserializedParents.clear();
	deserializedEntities.clear();
}

//**********************************************************************************************************************
void TransformSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<TransformFrame>(frame);
	if (frameView->animatePosition)
		serializer.write("position", (float3)frameView->position);
	if (frameView->animateScale)
		serializer.write("scale", (float3)frameView->scale);
	if (frameView->animateRotation)
		serializer.write("rotation", frameView->rotation);
}
void TransformSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<TransformFrame>(frame);
	frameView->animatePosition = deserializer.read("position", frameView->position, 3);
	frameView->animateScale = deserializer.read("scale", frameView->scale, 3);
	frameView->animateRotation = deserializer.read("rotation", frameView->rotation);
}

//**********************************************************************************************************************
void TransformSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<TransformComponent>(component);
	const auto frameA = View<TransformFrame>(a);
	const auto frameB = View<TransformFrame>(b);

	if (frameA->animatePosition)
		componentView->setPosition(lerp(frameA->position, frameB->position, t));
	if (frameA->animateScale)
		componentView->setScale(lerp(frameA->scale, frameB->scale, t));
	if (frameA->animateRotation)
		componentView->rotation = slerp(frameA->rotation, frameB->rotation, t);
}

//**********************************************************************************************************************
void TransformSystem::destroyRecursive(ID<Entity> entity)
{
	if (!entity)
		return;

	auto manager = Manager::Instance::get();
	if (manager->has<DoNotDestroyComponent>(entity))
		return;

	auto transformView = tryGetComponent(entity);
	if (!transformView)
	{
		manager->destroy(entity);
		return;
	}

	auto transformChildCount = transformView->childCount();
	auto transformChilds = transformView->childs;
	transformView->childCount() = 0;
	transformView->selfActive = false;

	for (uint32 i = 0; i < transformChildCount; i++)
		entityStack.push_back(transformChilds[i]);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		if (manager->has<DoNotDestroyComponent>(entity))
			continue;

		transformView = getComponent(entity);
		transformChildCount = transformView->childCount();
		transformChilds = transformView->childs;

		for (uint32 i = 0; i < transformChildCount; i++)
			entityStack.push_back(transformChilds[i]);

		transformView->parent = {};
		transformView->childCount() = 0;
		transformView->selfActive = false;
		transformView->ancestorsActive = true;		
		manager->destroy(entity);
	}
	
	manager->destroy(entity);
}
ID<Entity> TransformSystem::duplicateRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);

	auto manager = Manager::Instance::get();
	GARDEN_ASSERT(!manager->has<DoNotDuplicateComponent>(entity));

	auto entityDuplicate = manager->duplicate(entity);

	auto entityTransformView = tryGetComponent(entity);
	if (!entityTransformView)
		return entityDuplicate;

	auto duplicateTransformView = getComponent(entityDuplicate);
	duplicateTransformView->setParent(entityTransformView->getParent());
	entityDuplicateStack.emplace_back(entity, entityDuplicate);

	while (!entityDuplicateStack.empty())
	{
		auto pair = entityDuplicateStack.back();
		entityDuplicateStack.pop_back();

		if (manager->has<DoNotDuplicateComponent>(pair.first))
			continue;

		entityTransformView = getComponent(pair.first);
		auto entityChildCount = entityTransformView->childCount();
		auto entityChilds = entityTransformView->childs;

		for (uint32 i = 0; i < entityChildCount; i++)
		{
			auto child = entityChilds[i];
			auto duplicate = manager->duplicate(child);
			duplicateTransformView = getComponent(duplicate);
			duplicateTransformView->setParent(pair.second);
			entityDuplicateStack.emplace_back(child, duplicate);
		}
	}

	return entityDuplicate;
}

//**********************************************************************************************************************
StaticTransformSystem::StaticTransformSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);
}
StaticTransformSystem::~StaticTransformSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);
	unsetSingleton();
}

string_view StaticTransformSystem::getComponentName() const
{
	return "Static Transform";
}
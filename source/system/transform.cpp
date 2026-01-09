// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
		auto parentTransformView = Manager::Instance::get()->get<TransformComponent>(parent);
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
		auto manager = Manager::Instance::get();
		auto thisChildCount = childCount();

		for (uint32 i = 0; i < thisChildCount; i++)
		{
			auto childTransformView = manager->get<TransformComponent>(childs[i]);
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

	auto manager = Manager::Instance::get();
	entityStack.push_back(entity);

	if (isActive)
	{
		if (!ancestorsActive)
			return;

		while (!entityStack.empty())
		{
			auto entity = entityStack.back(); entityStack.pop_back();
			auto transformView = manager->get<TransformComponent>(entity);
			if (!transformView->selfActive)
				continue;

			auto childCount = transformView->childCount();
			auto childs = transformView->childs;

			for (uint32 i = 0; i < childCount; i++)
			{
				auto child = childs[i];
				auto childTransformView = manager->get<TransformComponent>(child);
				childTransformView->ancestorsActive = true;
				entityStack.push_back(child);
			}
		}
	}
	else
	{
		while (!entityStack.empty())
		{
			auto entity = entityStack.back(); entityStack.pop_back();
			auto transformView = manager->get<TransformComponent>(entity);
			auto childCount = transformView->childCount();
			auto childs = transformView->childs;

			for (uint32 i = 0; i < childCount; i++)
			{
				auto child = childs[i];
				auto childTransformView = manager->get<TransformComponent>(child);
				childTransformView->ancestorsActive = false;
				entityStack.push_back(child);
			}
		}
	}
}

//**********************************************************************************************************************
void TransformComponent::setParent(ID<Entity> parent)
{
	GARDEN_ASSERT(parent != entity);
	if (this->parent == parent)
		return;

	auto manager = Manager::Instance::get();
	#if GARDEN_DEBUG
	if (parent)
	{
		auto parentTransformView = manager->get<TransformComponent>(parent);
		GARDEN_ASSERT(!parentTransformView->hasAncestor(entity));
	}
	#endif

	if (this->parent)
	{
		auto parentTransformView = manager->get<TransformComponent>(this->parent);
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
		auto parentTransformView = manager->get<TransformComponent>(parent);
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
bool TransformComponent::tryAddChild(ID<Entity> child)
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto childTransformView = Manager::Instance::get()->get<TransformComponent>(child);
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
bool TransformComponent::tryRemoveChild(uint32 index) noexcept
{
	if (index >= getChildCount())
		return false;

	auto thisChildCount = childCount();
	auto child = childs[index];

	for (uint32 j = index + 1; j < thisChildCount; j++)
		childs[j - 1] = childs[j];

	auto childTransformView = Manager::Instance::get()->get<TransformComponent>(child);
	childTransformView->parent = {};
	childTransformView->ancestorsActive = true;

	childCount()--;
	return true;
}
bool TransformComponent::tryRemoveChild(ID<Entity> child) noexcept
{
	GARDEN_ASSERT(child);
	GARDEN_ASSERT(child != entity);

	auto thisChildCount = childCount();
	for (uint32 i = 0; i < thisChildCount; i++)
	{
		if (childs[i] != entity)
			continue;

		auto result = tryRemoveChild(i);
		GARDEN_ASSERT_MSG(result, "Detected memory corruption");
		return true;
	}

	return false;
}
void TransformComponent::removeAllChilds() noexcept
{
	auto manager = Manager::Instance::get();
	auto thisChildCount = childCount();

	for (uint32 i = 0; i < thisChildCount; i++)
	{
		auto childTransformView = manager->get<TransformComponent>(childs[i]);
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

	auto manager = Manager::Instance::get();
	auto nextParent = parent;

	while (nextParent)
	{
		auto nextTransformView = manager->get<TransformComponent>(nextParent);
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

	auto manager = Manager::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back(); entityStack.pop_back();
		auto transformView = manager->get<TransformComponent>(entity);
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
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back(); entityStack.pop_back();
		if (manager->has<StaticTransformComponent>(entity))
		{
			entityStack.clear();
			return true;
		}

		auto transformView = manager->get<TransformComponent>(entity);
		for (auto child : **transformView)
			entityStack.push_back(child);
	}

	return false;
}

#if GARDEN_EDITOR
void TransformComponent::resetUIDs()
{
	auto manager = Manager::Instance::get();
	stack<ID<Entity>, vector<ID<Entity>>> childEntities; childEntities.push(entity);

	while (!childEntities.empty())
	{
		auto childEntity = childEntities.top(); childEntities.pop();
		auto transformView = manager->get<TransformComponent>(childEntity);
		transformView->uid = 0;

		auto childCount = transformView->childCount();
		auto childs = transformView->childs;
		for (uint32 i = 0; i < childCount; i++)
			childEntities.push(childs[i]);
	}
}
#endif

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
	auto transformEditor = TransformEditorSystem::Instance::tryGet();
	if (transformEditor)
	{
		auto componentView = components.get(ID<TransformComponent>(instance));
		transformEditor->onEntityDestroy(componentView->entity);
	}
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
		auto parentView = manager->get<TransformComponent>(componentView->parent);
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
	auto boolValue = true;
	deserializer.read("isActive", boolValue);
	componentView->selfActive = boolValue;

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
	auto manager = Manager::Instance::get();
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

		auto transformView = manager->get<TransformComponent>(pair.first);
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
	if (frameView->animateIsActive)
		serializer.write("isActive", (bool)frameView->isActive);
}
void TransformSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<TransformFrame>(frame); auto isActive = true;
	frameView->animatePosition = deserializer.read("position", frameView->position, 3);
	frameView->animateScale = deserializer.read("scale", frameView->scale, 3);
	frameView->animateRotation = deserializer.read("rotation", frameView->rotation);
	frameView->animateIsActive = deserializer.read("isActive", isActive);
	frameView->isActive = isActive;
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
	if (frameA->animateIsActive)
		componentView->setActive((bool)round(t) ? frameB->isActive : frameA->isActive);
}

//**********************************************************************************************************************
void TransformSystem::destroyRecursive(ID<Entity> entity)
{
	if (!entity)
		return;

	auto manager = Manager::Instance::get();
	if (manager->has<DoNotDestroyComponent>(entity))
		return;

	auto transformView = manager->tryGet<TransformComponent>(entity);
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
		entityStack.push(transformChilds[i]);

	while (!entityStack.empty())
	{
		auto entity = entityStack.top();
		entityStack.pop();

		if (manager->has<DoNotDestroyComponent>(entity))
			continue;

		auto childTransformView = manager->get<TransformComponent>(entity);
		transformChildCount = childTransformView->childCount();
		transformChilds = childTransformView->childs;

		for (uint32 i = 0; i < transformChildCount; i++)
			entityStack.push(transformChilds[i]);

		childTransformView->parent = {};
		childTransformView->childCount() = 0;
		childTransformView->selfActive = false;
		childTransformView->ancestorsActive = true;		
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

	auto entityTransformView = manager->tryGet<TransformComponent>(entity);
	if (!entityTransformView)
		return entityDuplicate;

	auto duplicateTransformView = manager->get<TransformComponent>(entityDuplicate);
	duplicateTransformView->setParent(entityTransformView->getParent());
	entityDuplicateStack.emplace_back(entity, entityDuplicate);

	while (!entityDuplicateStack.empty())
	{
		auto pair = entityDuplicateStack.back();
		entityDuplicateStack.pop_back();

		if (manager->has<DoNotDuplicateComponent>(pair.first))
			continue;

		auto transformView = manager->get<TransformComponent>(pair.first);
		auto entityChildCount = transformView->childCount();
		auto entityChilds = transformView->childs;

		for (uint32 i = 0; i < entityChildCount; i++)
		{
			auto child = entityChilds[i];
			auto duplicate = manager->duplicate(child);
			duplicateTransformView = manager->get<TransformComponent>(duplicate);
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
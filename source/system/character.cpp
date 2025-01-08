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

#include "garden/system/character.hpp"
#include "garden/system/physics-impl.hpp"
#include "garden/system/transform.hpp"
#include "garden/profiler.hpp"
#include "math/matrix/transform.hpp"

#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"

using namespace ecsm;
using namespace garden;
using namespace garden::physics;

//**********************************************************************************************************************
bool CharacterComponent::destroy()
{
	if (instance)
	{
		auto instance = (JPH::CharacterVirtual*)this->instance;
		auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
			CharacterSystem::Instance::get()->charVsCharCollision;
		charVsCharCollision->Remove(instance);
		delete instance;

		this->shape = {};
		this->instance = nullptr;
	}
	return true;
}

//**********************************************************************************************************************
void CharacterComponent::setShape(ID<Shape> shape, float mass, float maxPenetrationDepth)
{
	if (this->shape == shape)
		return;

	if (shape)
	{
		auto physicsSystem = PhysicsSystem::Instance::get();
		auto shapeView = physicsSystem->get(shape);

		if (instance)
		{
			if (collisionLayer >= physicsSystem->properties.collisionLayerCount)
				collisionLayer = (uint16)CollisionLayer::Moving;

			auto instance = (JPH::CharacterVirtual*)this->instance;
			auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
			auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
			auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(collisionLayer);
			auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(collisionLayer);
			JPH::BodyID rigidbodyID = {};

			auto rigidbodyView = physicsSystem->tryGetComponent(entity);
			if (rigidbodyView && rigidbodyView->getShape())
			{
				auto instance = (JPH::Body*)rigidbodyView->instance;
				rigidbodyID = instance->GetID();
			}

			JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
			JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

			instance->SetShape((JPH::Shape*)shapeView->instance, maxPenetrationDepth,
				bpLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
			instance->SetMass(mass);
		}
		else
		{
			auto position = float3(0.0f); auto rotation = quat::identity;
			auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				position = transformView->position;
				rotation = transformView->rotation;
			}

			auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
			JPH::CharacterVirtualSettings settings;
			settings.mShape = (JPH::Shape*)shapeView->instance;
			settings.mMass = mass;
			auto instance = new JPH::CharacterVirtual(&settings, toVec3(position),
				toQuat(rotation), (JPH::uint64)*entity, physicsInstance);
			this->instance = instance;

			auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
				CharacterSystem::Instance::get()->charVsCharCollision;
			instance->SetCharacterVsCharacterCollision(charVsCharCollision);

			if (inSimulation)
				charVsCharCollision->Add(instance);
		}
	}
	else
	{
		if (instance)
		{
			auto instance = (JPH::CharacterVirtual*)this->instance;
			if (inSimulation)
			{
				auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
					CharacterSystem::Instance::get()->charVsCharCollision;
				charVsCharCollision->Remove(instance);
			}

			delete instance;
			this->instance = nullptr;
		}
	}

	this->shape = shape;
}

//**********************************************************************************************************************
float3 CharacterComponent::getPosition() const
{
	if (!shape)
		return float3(0.0f);
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toFloat3(instance->GetPosition());
}
void CharacterComponent::setPosition(const float3& position)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetPosition(toVec3(position));
}

quat CharacterComponent::getRotation() const
{
	if (!shape)
		return quat::identity;
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toQuat(instance->GetRotation());
}
void CharacterComponent::setRotation(const quat& rotation)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetRotation(toQuat(rotation));
}

void CharacterComponent::getPosAndRot(float3& position, quat& rotation) const
{
	if (!shape)
	{
		position = float3(0.0f);
		rotation = quat::identity;
		return;
	}

	auto instance = (const JPH::CharacterVirtual*)this->instance;
	position = toFloat3(instance->GetPosition());
	rotation = toQuat(instance->GetRotation());
}
void CharacterComponent::setPosAndRot(const float3& position, const quat& rotation)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetPosition(toVec3(position));
	instance->SetRotation(toQuat(rotation));
}
bool CharacterComponent::isPosAndRotChanged(const float3& position, const quat& rotation) const
{
	GARDEN_ASSERT(shape);
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return !instance->GetPosition().IsClose(toVec3(position)) || !instance->GetRotation().IsClose(toQuat(rotation));
}

//**********************************************************************************************************************
float3 CharacterComponent::getLinearVelocity() const
{
	if (!shape)
		return float3(0.0f);
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toFloat3(instance->GetLinearVelocity());
}
void CharacterComponent::setLinearVelocity(const float3& velocity)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetLinearVelocity(toVec3(velocity));
}

CharacterGround CharacterComponent::getGroundState() const
{
	GARDEN_ASSERT(shape);
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return (CharacterGround)instance->GetGroundState();
}

float CharacterComponent::getMass() const
{
	if (!shape)
		return 0.0f;
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return instance->GetMass();
}
void CharacterComponent::setMass(float mass)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetMass(mass);
}

//**********************************************************************************************************************
void CharacterComponent::update(float deltaTime, const float3& gravity)
{
	SET_CPU_ZONE_SCOPED("Character Update");

	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);

	if (transformView)
	{
		if (transformView->isActive())
		{
			if (!inSimulation)
			{
				auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
					CharacterSystem::Instance::get()->charVsCharCollision;
				charVsCharCollision->Add(instance);
				inSimulation = true;
			}
		}
		else
		{
			if (inSimulation)
			{
				auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
					CharacterSystem::Instance::get()->charVsCharCollision;
				charVsCharCollision->Remove(instance);
				inSimulation = false;
			}
			return;
		}
	}

	auto physicsSystem = PhysicsSystem::Instance::get();
	if (collisionLayer >= physicsSystem->properties.collisionLayerCount)
		collisionLayer = (uint16)CollisionLayer::Moving;
	
	auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
	auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
	auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(collisionLayer);
	auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(collisionLayer);
	JPH::BodyID rigidbodyID = {};

	auto rigidbodyView = physicsSystem->tryGetComponent(entity);
	if (rigidbodyView && rigidbodyView->instance)
	{
		auto instance = (JPH::Body*)rigidbodyView->instance;
		rigidbodyID = instance->GetID();
	}

	JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
	JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

	instance->Update(deltaTime, toVec3(gravity), bpLayerFilter, 
		objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
	
	if (transformView)
	{
		float3 position; quat rotation;
		getPosAndRot(position, rotation);

		if (transformView->getParent())
		{
			auto parentView = TransformSystem::Instance::get()->getComponent(transformView->getParent());
			auto model = parentView->calcModel();
			position = (float3)(inverse(model) * float4(position, 1.0f));
			rotation *= inverse(extractQuat(extractRotation(model)));
		}

		transformView->position = position;
		transformView->rotation = rotation;
	}

	if (rigidbodyView && rigidbodyView->instance)
	{
		float3 position; quat rotation;
		getPosAndRot(position, rotation);

		if (rigidbodyView->isPosAndRotChanged(position, rotation))
			rigidbodyView->setPosAndRot(position, rotation, true);
	}
}
void CharacterComponent::extendedUpdate()
{
	abort(); // TODO:
}

void CharacterComponent::setWorldTransform()
{
	GARDEN_ASSERT(shape);
	auto transformView = TransformSystem::Instance::get()->getComponent(entity);
	auto model = transformView->calcModel();
	setPosAndRot(getTranslation(model), extractQuat(extractRotation(model)));
}

//**********************************************************************************************************************
CharacterSystem::CharacterSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto charVsCharCollision = new JPH::CharacterVsCharacterCollisionSimple();
	this->charVsCharCollision = charVsCharCollision;
}
CharacterSystem::~CharacterSystem()
{
	if (Manager::Instance::get()->isRunning)
		delete (JPH::CharacterVsCharacterCollisionSimple*)charVsCharCollision;
	else
		components.clear(false);
	unsetSingleton();
}

//**********************************************************************************************************************
void CharacterSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<CharacterComponent>(source);
	auto destinationView = View<CharacterComponent>(destination);
	destinationView->destroy();

	destinationView->inSimulation = sourceView->inSimulation;
	destinationView->setShape(sourceView->shape, sourceView->getMass());

	float3 position; quat rotation;
	sourceView->getPosAndRot(position, rotation);
	destinationView->setPosAndRot(position, rotation);
	destinationView->setLinearVelocity(sourceView->getLinearVelocity());
}
const string& CharacterSystem::getComponentName() const
{
	static const string name = "Character";
	return name;
}

//**********************************************************************************************************************
void CharacterSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto characterView = View<CharacterComponent>(component);

	if (characterView->shape)
	{
		auto mass = characterView->getMass();
		if (mass != 70.0f)
			serializer.write("mass", mass);

		float3 position; quat rotation;
		characterView->getPosAndRot(position, rotation);
		if (position != float3(0.0f))
			serializer.write("position", position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

		auto velocity = characterView->getLinearVelocity();
		if (velocity != float3(0.0f))
			serializer.write("linearVelocity", position);

		auto physicsSystem = PhysicsSystem::Instance::get();
		physicsSystem->serializeDecoratedShape(serializer, characterView->shape);
	}
}
void CharacterSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto characterView = View<CharacterComponent>(component);

	if (deserializer.read("shapeType", valueStringCache))
	{
		auto physicsSystem = PhysicsSystem::Instance::get();
		auto shape = physicsSystem->deserializeDecoratedShape(deserializer, valueStringCache);
		if (shape)
		{
			auto mass = 70.0f;
			deserializer.read("mass", mass);
			characterView->setShape(shape, mass);

			auto position = float3(0.0f);
			deserializer.read("position", position);
			auto rotation = quat::identity;
			deserializer.read("rotation", rotation);
			if (position != float3(0.0f) || rotation != quat::identity)
				characterView->setPosAndRot(position, rotation);

			auto velocity = float3(0.0f);
			deserializer.read("linearVelocity", velocity);
			if (velocity != float3(0.0f))
				characterView->setLinearVelocity(velocity);
		}
	}
}

//**********************************************************************************************************************
void CharacterSystem::setWorldTransformRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);

	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto characterView = tryGetComponent(entity);
		if (characterView && characterView->getShape())
			characterView->setWorldTransform();

		auto transformView = transformSystem->tryGetComponent(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}
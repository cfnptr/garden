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

			auto rigidbodyView = Manager::Instance::get()->tryGet<RigidbodyComponent>(entity);
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
			auto position = f32x4::zero; auto rotation = quat::identity;
			auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				position = transformView->getPosition();
				rotation = transformView->getRotation();
			}

			auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
			JPH::CharacterVirtualSettings settings;
			settings.mShape = (JPH::Shape*)shapeView->instance;
			settings.mMass = mass;
			settings.mEnhancedInternalEdgeRemoval = true;
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
f32x4 CharacterComponent::getPosition() const
{
	if (!shape)
		return f32x4::zero;
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toF32x4(instance->GetPosition());
}
void CharacterComponent::setPosition(f32x4 position)
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
void CharacterComponent::setRotation(quat rotation)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetRotation(toQuat(rotation));
}

void CharacterComponent::getPosAndRot(f32x4& position, quat& rotation) const
{
	if (!shape)
	{
		position = f32x4::zero;
		rotation = quat::identity;
		return;
	}

	auto instance = (const JPH::CharacterVirtual*)this->instance;
	position = toF32x4(instance->GetPosition());
	rotation = toQuat(instance->GetRotation());
}
void CharacterComponent::setPosAndRot(f32x4 position, quat rotation)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetPosition(toVec3(position));
	instance->SetRotation(toQuat(rotation));
}
bool CharacterComponent::isPosAndRotChanged(f32x4 position, quat rotation) const
{
	GARDEN_ASSERT(shape);
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return !instance->GetPosition().IsClose(toVec3(position)) || !instance->GetRotation().IsClose(toQuat(rotation));
}

//**********************************************************************************************************************
f32x4 CharacterComponent::getLinearVelocity() const
{
	if (!shape)
		return f32x4::zero;
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toF32x4(instance->GetLinearVelocity());
}
void CharacterComponent::setLinearVelocity(f32x4 velocity)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	instance->SetLinearVelocity(toVec3(velocity));
}

f32x4 CharacterComponent::getGroundVelocity() const
{
	if (!shape)
		return f32x4::zero;
	auto instance = (const JPH::CharacterVirtual*)this->instance;
	return toF32x4(instance->GetGroundVelocity());
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
void CharacterComponent::update(float deltaTime, f32x4 gravity, const UpdateSettings* settings)
{
	SET_CPU_ZONE_SCOPED("Character Update");

	GARDEN_ASSERT(shape);
	auto manager = Manager::Instance::get();
	auto instance = (JPH::CharacterVirtual*)this->instance;
	auto transformView = manager->tryGet<TransformComponent>(entity);

	if (transformView)
	{
		auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
			CharacterSystem::Instance::get()->charVsCharCollision;
		if (transformView->isActive())
		{
			if (!inSimulation)
			{
				charVsCharCollision->Add(instance);
				inSimulation = true;
			}
		}
		else
		{
			if (inSimulation)
			{
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

	auto rigidbodyView = manager->tryGet<RigidbodyComponent>(entity);
	if (rigidbodyView && rigidbodyView->instance)
	{
		auto instance = (JPH::Body*)rigidbodyView->instance;
		rigidbodyID = instance->GetID();
	}

	JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
	JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

	if (settings)
	{
		JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
		updateSettings.mStickToFloorStepDown = toVec3(settings->stepDown);
		updateSettings.mWalkStairsStepUp = toVec3(settings->stepUp);
		updateSettings.mWalkStairsMinStepForward = settings->minStepForward;
		updateSettings.mWalkStairsStepForwardTest = settings->stepForwardTest;
		updateSettings.mWalkStairsCosAngleForwardContact = settings->forwardContact;
		updateSettings.mWalkStairsStepDownExtra = toVec3(settings->stepDownExtra);
		instance->ExtendedUpdate(deltaTime, toVec3(gravity), updateSettings, 
			bpLayerFilter, objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
	}
	else
	{
		instance->Update(deltaTime, toVec3(gravity), bpLayerFilter, 
			objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
	}
	
	if (transformView)
	{
		f32x4 position; quat rotation;
		getPosAndRot(position, rotation);

		if (transformView->getParent())
		{
			auto parentView = manager->get<TransformComponent>(transformView->getParent());
			auto model = parentView->calcModel();
			position = inverse4x4(model) * f32x4(position, 1.0f);
			rotation *= inverse(extractQuat(extractRotation(model)));
		}

		transformView->setPosition(position);
		transformView->setRotation(rotation);
	}

	if (rigidbodyView && rigidbodyView->instance)
	{
		f32x4 position; quat rotation;
		getPosAndRot(position, rotation);

		if (rigidbodyView->isPosAndRotChanged(position, rotation))
			rigidbodyView->setPosAndRot(position, rotation, true);
	}
}

//**********************************************************************************************************************
bool CharacterComponent::canWalkStairs(f32x4 linearVelocity) const
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	return instance->CanWalkStairs(toVec3(linearVelocity));
}
bool CharacterComponent::walkStairs(float deltaTime, f32x4 stepUp, 
	f32x4 stepForward, f32x4 stepForwardTest, f32x4 stepDownExtra)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;

	auto physicsSystem = PhysicsSystem::Instance::get();
	if (collisionLayer >= physicsSystem->properties.collisionLayerCount)
		collisionLayer = (uint16)CollisionLayer::Moving;

	auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
	auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
	auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(collisionLayer);
	auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(collisionLayer);
	JPH::BodyID rigidbodyID = {};

	auto rigidbodyView = Manager::Instance::get()->tryGet<RigidbodyComponent>(entity);
	if (rigidbodyView && rigidbodyView->instance)
	{
		auto instance = (JPH::Body*)rigidbodyView->instance;
		rigidbodyID = instance->GetID();
	}

	JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
	JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

	return instance->WalkStairs(deltaTime, toVec3(stepUp), toVec3(stepForward), 
		toVec3(stepForwardTest), toVec3(stepDownExtra), bpLayerFilter, 
		objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
}

bool CharacterComponent::stickToFloor(f32x4 stepDown)
{
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;

	auto physicsSystem = PhysicsSystem::Instance::get();
	if (collisionLayer >= physicsSystem->properties.collisionLayerCount)
		collisionLayer = (uint16)CollisionLayer::Moving;

	auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
	auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
	auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(collisionLayer);
	auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(collisionLayer);
	JPH::BodyID rigidbodyID = {};

	auto rigidbodyView = Manager::Instance::get()->tryGet<RigidbodyComponent>(entity);
	if (rigidbodyView && rigidbodyView->instance)
	{
		auto instance = (JPH::Body*)rigidbodyView->instance;
		rigidbodyID = instance->GetID();
	}

	JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
	JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

	return instance->StickToFloor(toVec3(stepDown),  bpLayerFilter, 
		objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);
}

void CharacterComponent::setWorldTransform()
{
	GARDEN_ASSERT(shape);
	auto transformView = Manager::Instance::get()->get<TransformComponent>(entity);
	auto model = transformView->calcModel();
	setPosAndRot(getTranslation(model), extractQuat(extractRotation(model)));
}

//**********************************************************************************************************************
CharacterSystem::CharacterSystem(bool setSingleton) : Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<ISerializable>(this);
	this->charVsCharCollision = new JPH::CharacterVsCharacterCollisionSimple();
}
CharacterSystem::~CharacterSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		delete (JPH::CharacterVsCharacterCollisionSimple*)charVsCharCollision;
		Manager::Instance::get()->removeGroupSystem<ISerializable>(this);
	}
	unsetSingleton();
}

ID<Component> CharacterSystem::createComponent(ID<Entity> entity)
{
	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
	if (transformView)
		transformView->modelWithAncestors = true;
	return ID<Component>(components.create());
}
void CharacterSystem::destroyComponent(ID<Component> instance)
{
	auto manager = Manager::Instance::get();
	auto componentView = getComponent(instance);
	auto transformView = manager->tryGet<TransformComponent>(componentView->getEntity());
	auto rigidbodyView = manager->tryGet<RigidbodyComponent>(componentView->getEntity());
	if (transformView && rigidbodyView)
		transformView->modelWithAncestors = rigidbodyView->getMotionType() == MotionType::Static;
	components.destroy(ID<CharacterComponent>(instance));
}
void CharacterSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<CharacterComponent>(component);
	componentView->setShape({});

	if (full)
		**componentView = {};
}
void CharacterSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<CharacterComponent>(source);
	auto destinationView = View<CharacterComponent>(destination);
	destinationView->inSimulation = sourceView->inSimulation;
	destinationView->setShape(sourceView->shape, sourceView->getMass());

	f32x4 position; quat rotation;
	sourceView->getPosAndRot(position, rotation);
	destinationView->setPosAndRot(position, rotation);
	destinationView->setLinearVelocity(sourceView->getLinearVelocity());
}
string_view CharacterSystem::getComponentName() const
{
	return "Character";
}

//**********************************************************************************************************************
void CharacterSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<CharacterComponent>(component);
	if (componentView->shape)
	{
		auto mass = componentView->getMass();
		if (mass != 70.0f)
			serializer.write("mass", mass);

		f32x4 position; quat rotation;
		componentView->getPosAndRot(position, rotation);
		if (position != f32x4::zero)
			serializer.write("position", (float3)position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

		auto velocity = componentView->getLinearVelocity();
		if (velocity != f32x4::zero)
			serializer.write("linearVelocity", (float3)position);

		auto physicsSystem = PhysicsSystem::Instance::get();
		physicsSystem->serializeDecoratedShape(serializer, componentView->shape);
	}
}
void CharacterSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<CharacterComponent>(component);
	if (deserializer.read("shapeType", valueStringCache))
	{
		auto physicsSystem = PhysicsSystem::Instance::get();
		auto shape = physicsSystem->deserializeDecoratedShape(deserializer, valueStringCache);
		if (shape)
		{
			auto mass = 70.0f;
			deserializer.read("mass", mass);
			componentView->setShape(shape, mass);

			auto f32x4Value = f32x4::zero; auto rotation = quat::identity;
			deserializer.read("position", f32x4Value, 3);
			deserializer.read("rotation", rotation);
			if (f32x4Value != f32x4::zero || rotation != quat::identity)
				componentView->setPosAndRot(f32x4Value, rotation);

			f32x4Value = f32x4::zero;
			deserializer.read("linearVelocity", f32x4Value, 3);
			if (f32x4Value != f32x4::zero)
				componentView->setLinearVelocity(f32x4Value);
		}
	}
}

//**********************************************************************************************************************
void CharacterSystem::setWorldTransformRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);

	auto manager = Manager::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto characterView = manager->tryGet<CharacterComponent>(entity);
		if (characterView && characterView->getShape())
			characterView->setWorldTransform();

		auto transformView = manager->tryGet<TransformComponent>(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}
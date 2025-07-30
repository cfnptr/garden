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

#include "garden/system/physics.hpp"
#include "garden/system/physics-impl.hpp"
#include "garden/system/character.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/system/log.hpp"
#include "garden/profiler.hpp"
#include "garden/base64.hpp"
#include "math/matrix/transform.hpp"

#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Core/FixedSizeFreeList.h"
#include "Jolt/Core/JobSystemWithBarrier.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/NarrowPhaseStats.h"
#include "Jolt/Physics/Collision/CollidePointResult.h"
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/ObjectLayerPairFilterTable.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h"
#include "Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/EmptyShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Constraints/PointConstraint.h"

using namespace garden;
using namespace garden::physics;

//**********************************************************************************************************************
static void TraceImpl(const char* inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	GARDEN_LOG_TRACE(buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
	GARDEN_LOG_TRACE(string(inFile) + ":" + to_string(inLine) + ": (" +
		inExpression + ") " + (inMessage != nullptr ? inMessage : ""));
	return true;
};
#endif

//**********************************************************************************************************************
class GardenJobSystem final : public JPH::JobSystemWithBarrier
{
	ThreadPool* threadPool = nullptr;

	/// Array of jobs (fixed size)
	using AvailableJobs = JPH::FixedSizeFreeList<Job>;
	AvailableJobs jobs;
public:
	GardenJobSystem(ThreadPool* threadPool)
	{
		GARDEN_ASSERT(threadPool);
		this->threadPool = threadPool;

		JobSystemWithBarrier::Init(JPH::cMaxPhysicsBarriers);
		jobs.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsJobs); // Init freelist of jobs
	}

	int GetMaxConcurrency() const final
	{
		return (int)threadPool->getThreadCount();
	}

	JobHandle CreateJob(const char* inName, JPH::ColorArg inColor,
		const JobFunction& inJobFunction, JPH::uint32 inNumDependencies = 0) final
	{
		// Loop until we can get a job from the free list
		JPH::uint32 index;
		while (true)
		{
			index = jobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
			if (index != AvailableJobs::cInvalidObjectIndex)
				break;

			JPH_ASSERT(false, "No jobs available!");
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
		Job* job = &jobs.Get(index);

		// Construct handle to keep a reference, the job is queued below and may immediately complete
		JobHandle handle(job);

		// If there are no dependencies, queue the job now
		if (inNumDependencies == 0)
			QueueJob(job);

		// Return the handle
		return handle;
	}

	ThreadPool* getThreadPool() const noexcept { return threadPool; }
protected:
	void QueueJob(Job* inJob) final
	{
		// Add reference to job because we're adding the job to the queue
		inJob->AddRef();

		threadPool->addTask([inJob](const ThreadPool::Task& task)
		{
			inJob->Execute();
			inJob->Release();
		});
	}
	void QueueJobs(Job** inJobs, JPH::uint inNumJobs) final
	{
		static thread_local vector<ThreadPool::Task::Function> functions;
		functions.resize(inNumJobs);

		for (JPH::uint i = 0; i < inNumJobs; i++)
		{
			auto job = inJobs[i];

			// Add reference to job because we're adding the job to the queue
			job->AddRef();

			functions[i] = [job](const ThreadPool::Task& task)
			{
				job->Execute();
				job->Release();
			};
		}

		threadPool->addTasks(functions);
	}
	void FreeJob(Job* inJob) final
	{
		jobs.DestructObject(inJob);
	}
};

//**********************************************************************************************************************
class GardenContactListener final : public JPH::ContactListener
{
	PhysicsSystem* physicsSystem = nullptr;
	vector<PhysicsSystem::Event>* bodyEvents = nullptr;
	mutex* bodyEventLocker = nullptr;
public:
	GardenContactListener(PhysicsSystem* physicsSystem, 
		vector<PhysicsSystem::Event>* bodyEvents, mutex* bodyEventLocker) :
		physicsSystem(physicsSystem), bodyEvents(bodyEvents), bodyEventLocker(bodyEventLocker) { }

	JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) final
	{
		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void addEvent(uint32 userData1, uint32 userData2, BodyEvent eventType)
	{
		ID<Entity> entity1, entity2;
		*entity1 = userData1; *entity2 = userData2;
		auto rigidbodyView1 = physicsSystem->getComponent(entity1);
		auto rigidbodyView2 = physicsSystem->getComponent(entity2);

		if (!rigidbodyView1->eventListener.empty() || !rigidbodyView2->eventListener.empty())
		{
			PhysicsSystem::Event bodyEvent;
			bodyEvent.eventType = eventType;
			bodyEvent.data1 = userData1;
			bodyEvent.data2 = userData2;
			bodyEventLocker->lock();
			bodyEvents->push_back(bodyEvent);
			bodyEventLocker->unlock();
		}
	}

	void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) final
	{
		addEvent((uint32)inBody1.GetUserData(), (uint32)inBody2.GetUserData(), BodyEvent::Entered);
	}
	void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) final
	{
		addEvent((uint32)inBody1.GetUserData(), (uint32)inBody2.GetUserData(), BodyEvent::Stayed);
	}
	void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) final
	{
		PhysicsSystem::Event bodyEvent;
		bodyEvent.eventType = BodyEvent::Exited;
		bodyEvent.data1 = inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber();
		bodyEvent.data2 = inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber();
		bodyEventLocker->lock();
		bodyEvents->push_back(bodyEvent);
		bodyEventLocker->unlock();
	}
};

// TODO: add CharacterContactListener

//**********************************************************************************************************************
class GardenBodyActivationListener final : public JPH::BodyActivationListener
{
	PhysicsSystem* physicsSystem = nullptr;
	vector<PhysicsSystem::Event>* bodyEvents = nullptr;
	mutex* bodyEventLocker = nullptr;
public:
	GardenBodyActivationListener(PhysicsSystem* _physicsSystem,
		vector<PhysicsSystem::Event>* _bodyEvents, mutex* _bodyEventLocker) :
		physicsSystem(_physicsSystem), bodyEvents(_bodyEvents), bodyEventLocker(_bodyEventLocker) { }

	void addEvent(uint32 inBodyUserData, BodyEvent eventType)
	{
		PhysicsSystem::Event bodyEvent;
		bodyEvent.eventType = eventType;
		bodyEvent.data1 = inBodyUserData;
		bodyEventLocker->lock();
		bodyEvents->push_back(bodyEvent);
		bodyEventLocker->unlock();
	}

	void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
		ID<Entity> entity; *entity = inBodyUserData;
		auto rigidbodyView = physicsSystem->tryGetComponent(entity);

		if (rigidbodyView && !rigidbodyView->eventListener.empty())
			addEvent((uint32)inBodyUserData, BodyEvent::Activated);
	}
	void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
		ID<Entity> entity; *entity = inBodyUserData;
		if (!entity)
			return;

		auto rigidbodyView = physicsSystem->getComponent(entity);
		if (!rigidbodyView->eventListener.empty())
			addEvent((uint32)inBodyUserData, BodyEvent::Deactivated);
	}
};

//**********************************************************************************************************************
bool Shape::destroy()
{
	if (!instance)
		return true;

	auto instance = (JPH::Shape*)this->instance;

	ID<Shape> innerShape = {};
	if (instance->GetType() == JPH::EShapeType::Decorated)
	{
		auto decoratedInstance = (JPH::DecoratedShape*)this->instance;
		*innerShape = (uint32)decoratedInstance->GetInnerShape()->GetUserData();
	}

	instance->Release();

	if (innerShape)
	{
		auto physicsSystem = PhysicsSystem::Instance::get();
		auto innerView = physicsSystem->get(innerShape);
		if (innerView->instance)
			physicsSystem->destroyShared(innerShape);
	}

	return true;
}

ShapeType Shape::getType() const
{
	GARDEN_ASSERT(instance);
	static_assert((uint32)ShapeType::User4 == (uint32)JPH::EShapeType::User4, 
		"ShapeType does not map to the EShapeType");
	auto instance = (const JPH::Shape*)this->instance;
	return (ShapeType)instance->GetType();
}
ShapeSubType Shape::getSubType() const
{
	GARDEN_ASSERT(instance);
	static_assert((uint32)ShapeSubType::Empty == (uint32)JPH::EShapeSubType::Empty,
		"ShapeSubType does not map to the EShapeSubType");
	auto instance = (const JPH::Shape*)this->instance;
	return (ShapeSubType)instance->GetSubType();
}

f32x4 Shape::getCenterOfMass() const
{
	auto instance = (const JPH::Shape*)this->instance;
	return toF32x4(instance->GetCenterOfMass());
}
float Shape::getVolume() const
{
	auto instance = (const JPH::Shape*)this->instance;
	return instance->GetVolume();
}
void Shape::getMassProperties(float& mass, f32x4x4& inertia) const
{
	auto instance = (const JPH::Shape*)this->instance;
	auto massProperties = instance->GetMassProperties();
	mass = massProperties.mMass;
	inertia = toF32x4x4(massProperties.mInertia);
}

float Shape::getDensity() const
{
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetType() == JPH::EShapeType::Convex);
	auto convexInstance = (const JPH::ConvexShape*)instance;
	return convexInstance->GetDensity();
}

//**********************************************************************************************************************
f32x4 Shape::getBoxHalfExtent() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (const JPH::BoxShape*)instance;
	return toF32x4(boxInstance->GetHalfExtent());
}
float Shape::getBoxConvexRadius() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (const JPH::BoxShape*)instance;
	return boxInstance->GetConvexRadius();
}

float Shape::getSphereRadius() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Sphere);
	auto sphereInstance = (const JPH::SphereShape*)instance;
	return sphereInstance->GetRadius();
}

float Shape::getCapsuleHalfHeight() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Capsule);
	auto capsuleInstance = (const JPH::CapsuleShape*)instance;
	return capsuleInstance->GetHalfHeightOfCylinder();
}
float Shape::getCapsuleRadius() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Capsule);
	auto capsuleInstance = (const JPH::CapsuleShape*)instance;
	return capsuleInstance->GetRadius();
}

//**********************************************************************************************************************
ID<Shape> Shape::getInnerShape() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetType() == JPH::EShapeType::Decorated);
	auto decoratedInstance = (const JPH::DecoratedShape*)instance;
	auto innerInstance = decoratedInstance->GetInnerShape();
	ID<Shape> id; *id = (uint32)innerInstance->GetUserData();
	return id;
}
f32x4 Shape::getPosition() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::RotatedTranslated);
	auto rotTransInstance = (const JPH::RotatedTranslatedShape*)instance;
	return toF32x4(rotTransInstance->GetPosition());
}
quat Shape::getRotation() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::RotatedTranslated);
	auto rotTransInstance = (const JPH::RotatedTranslatedShape*)instance;
	return toQuat(rotTransInstance->GetRotation());
}
void Shape::getPosAndRot(f32x4& position, quat& rotation) const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::RotatedTranslated);
	auto rotTransInstance = (const JPH::RotatedTranslatedShape*)instance;
	position = toF32x4(rotTransInstance->GetPosition());
	rotation = toQuat(rotTransInstance->GetRotation());
}

uint64 Shape::getRefCount() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	return instance->GetRefCount();
}
bool Shape::isLastRef() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	return instance->GetRefCount() == 1;
}

//**********************************************************************************************************************
void RigidbodyComponent::setShape(ID<Shape> shape, MotionType motionType, int32 collisionLayer, 
	bool activate, bool allowDynamicOrKinematic, AllowedDOF allowedDOF)
{
	if (this->shape == shape)
		return;

	auto body = (JPH::Body*)instance;
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;

	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);
		auto shapeInstance = (JPH::Shape*)shapeView->instance;

		if (instance)
		{
			bodyInterface->SetShape(body->GetID(), shapeInstance, true, activate && inSimulation ? 
				JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		}
		else
		{
			auto position = f32x4::zero; auto rotation = quat::identity;
			auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				transformView->modelWithAncestors = motionType == MotionType::Static || 
					Manager::Instance::get()->has<CharacterComponent>(entity);
				position = this->lastPosition = transformView->getPosition();
				rotation = this->lastRotation = transformView->getRotation();
			}

			if (collisionLayer < 0 || collisionLayer >= physicsSystem->properties.collisionLayerCount)
			{
				collisionLayer = motionType == MotionType::Static ?
					(int32)CollisionLayer::NonMoving : (int32)CollisionLayer::Moving;
			}

			JPH::BodyCreationSettings settings(shapeInstance, toRVec3(position), toQuat(rotation), 
				(JPH::EMotionType)motionType, (JPH::ObjectLayer)collisionLayer);
			settings.mUserData = *entity;
			settings.mAllowDynamicOrKinematic = allowDynamicOrKinematic;
			settings.mAllowedDOFs = (JPH::EAllowedDOFs)allowedDOF;

			auto body = bodyInterface->CreateBody(settings);
			if (inSimulation)
			{
				bodyInterface->AddBody(body->GetID(), activate ?
					JPH::EActivation::Activate : JPH::EActivation::DontActivate);
			}

			this->instance = body;
		}
	}
	else
	{
		if (instance)
		{
			if (inSimulation)
				bodyInterface->RemoveBody(body->GetID());
			bodyInterface->DestroyBody(body->GetID());
			this->instance = nullptr;
		}

		auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
		if (transformView)
			transformView->modelWithAncestors = true;
	}

	this->shape = shape;
}

//**********************************************************************************************************************
void RigidbodyComponent::notifyShapeChanged(f32x4 previousCenterOfMass, bool updateMassProperties, bool activate)
{
	if (!shape)
		return;

	auto body = (JPH::Body*)instance;
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;
	
	bodyInterface->NotifyShapeChanged(body->GetID(), toVec3(previousCenterOfMass), 
		updateMassProperties, activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void RigidbodyComponent::setInSimulation(bool inSimulation)
{
	if (this->inSimulation == inSimulation)
		return;

	if (instance)
	{
		auto body = (JPH::Body*)instance;
		auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;

		if (inSimulation)
			bodyInterface->AddBody(body->GetID(), JPH::EActivation::Activate);
		else
			bodyInterface->RemoveBody(body->GetID());
	}
	
	this->inSimulation = inSimulation;
}

bool RigidbodyComponent::canBeKinematicOrDynamic() const
{
	if (!shape)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->CanBeKinematicOrDynamic();
}
AllowedDOF RigidbodyComponent::getAllowedDOF() const
{
	if (!shape || getMotionType() == MotionType::Static)
		return AllowedDOF::None;
	auto body = (const JPH::Body*)instance;
	return (AllowedDOF)body->GetMotionProperties()->GetAllowedDOFs();
}
uint8 RigidbodyComponent::getBroadPhaseLayer() const
{
	if (!shape)
		return (uint8)BroadPhaseLayer::NonMoving;
	auto body = (const JPH::Body*)instance;
	return (uint8)body->GetBroadPhaseLayer().GetValue();
}

//**********************************************************************************************************************
MotionType RigidbodyComponent::getMotionType() const
{
	if (!shape)
		return MotionType::Static;
	auto body = (const JPH::Body*)instance;
	return (MotionType)body->GetMotionType();
}
void RigidbodyComponent::setMotionType(MotionType motionType, bool activate)
{
	if (!shape)
		return;

	if (getMotionType() == motionType)
	{
		if (activate && inSimulation)
		{
			auto body = (JPH::Body*)instance;
			auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
			bodyInterface->ActivateBody(body->GetID());
		}
		return;
	}

	if (getMotionType() == MotionType::Static && motionType != MotionType::Static)
	{
		GARDEN_ASSERT(canBeKinematicOrDynamic());
	}

	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->SetMotionType(body->GetID(), (JPH::EMotionType)motionType,
		activate && inSimulation ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
	if (transformView)
	{
		transformView->modelWithAncestors = motionType == MotionType::Static || 
			Manager::Instance::get()->has<CharacterComponent>(entity);
	}
}

uint16 RigidbodyComponent::getCollisionLayer() const
{
	if (!shape)
		return (uint8)CollisionLayer::NonMoving;
	auto body = (const JPH::Body*)instance;
	return (uint16)body->GetObjectLayer();
}
void RigidbodyComponent::setCollisionLayer(int32 collisionLayer)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;
	if (collisionLayer < 0 || collisionLayer >= physicsSystem->properties.collisionLayerCount)
	{
		collisionLayer = getMotionType() == MotionType::Static ?
			(int32)CollisionLayer::NonMoving : (int32)CollisionLayer::Moving;
	}
	bodyInterface->SetObjectLayer(body->GetID(), (JPH::ObjectLayer)collisionLayer);
}

//**********************************************************************************************************************
bool RigidbodyComponent::isActive() const
{
	if (!shape)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->IsActive();
}
void RigidbodyComponent::activate()
{
	if (!shape || !inSimulation)
		return;
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->ActivateBody(body->GetID());
}
void RigidbodyComponent::deactivate()
{
	if (!shape || !inSimulation)
		return;
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->DeactivateBody(body->GetID());
}

bool RigidbodyComponent::isSensor() const
{
	if (!shape)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->IsSensor();
}
void RigidbodyComponent::setSensor(bool isSensor)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	return body->SetIsSensor(isSensor);
}

bool RigidbodyComponent::isKinematicVsStatic() const
{
	if (!shape)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->GetCollideKinematicVsNonDynamic();
}
void RigidbodyComponent::setKinematicVsStatic(bool isKinematicVsStatic)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	return body->SetCollideKinematicVsNonDynamic(isKinematicVsStatic);
}

//**********************************************************************************************************************
f32x4 RigidbodyComponent::getPosition() const
{
	if (!shape)
		return f32x4::zero;
	auto body = (const JPH::Body*)instance;
	return toF32x4(body->GetPosition());
}
void RigidbodyComponent::setPosition(f32x4 position, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->SetPosition(body->GetID(), toVec3(position), activate && inSimulation ? 
		JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = position;
}

quat RigidbodyComponent::getRotation() const
{
	if (!shape)
		return quat::identity;
	auto body = (const JPH::Body*)instance;
	return toQuat(body->GetRotation());
}
void RigidbodyComponent::setRotation(quat rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->SetRotation(body->GetID(), toQuat(rotation), activate && inSimulation ? 
		JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastRotation = rotation;
}

void RigidbodyComponent::getPosAndRot(f32x4& position, quat& rotation) const
{
	if (!shape)
	{
		position = f32x4::zero;
		rotation = quat::identity;
		return;
	}

	auto body = (const JPH::Body*)instance;
	position = toF32x4(body->GetPosition());
	rotation = toQuat(body->GetRotation());
}
void RigidbodyComponent::setPosAndRot(f32x4 position, quat rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->SetPositionAndRotation(body->GetID(), toVec3(position), toQuat(rotation),
		activate && inSimulation ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = position;
	lastRotation = rotation;
}
bool RigidbodyComponent::isPosAndRotChanged(f32x4 position, quat rotation) const
{
	GARDEN_ASSERT(shape);
	auto body = (const JPH::Body*)instance;
	return !body->GetPosition().IsClose(toVec3(position)) || !body->GetRotation().IsClose(toQuat(rotation));
}

//**********************************************************************************************************************
f32x4 RigidbodyComponent::getLinearVelocity() const
{
	if (!shape)
		return f32x4::zero;
	auto body = (const JPH::Body*)instance;
	return toF32x4(body->GetLinearVelocity());
}
void RigidbodyComponent::setLinearVelocity(f32x4 velocity)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(getMotionType() != MotionType::Static);
	auto body = (JPH::Body*)instance;
	body->SetLinearVelocity(toVec3(velocity));
}

f32x4 RigidbodyComponent::getAngularVelocity() const
{
	if (!shape)
		return f32x4::zero;
	auto body = (const JPH::Body*)instance;
	return toF32x4(body->GetAngularVelocity());
}
void RigidbodyComponent::setAngularVelocity(f32x4 velocity)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(getMotionType() != MotionType::Static);
	auto body = (JPH::Body*)instance;
	body->SetAngularVelocity(toVec3(velocity));
}

f32x4 RigidbodyComponent::getPointVelocity(f32x4 point) const
{
	if (!shape)
		return f32x4::zero;
	auto body = (const JPH::Body*)instance;
	return toF32x4(body->GetPointVelocity(toVec3(point)));
}
f32x4 RigidbodyComponent::getPointVelocityCOM(f32x4 point) const
{
	if (!shape)
		return f32x4::zero;
	auto body = (JPH::Body*)instance;
	return toF32x4(body->GetPointVelocityCOM(toVec3(point)));
}

void RigidbodyComponent::moveKinematic(f32x4 position, quat rotation, float deltaTime)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(getMotionType() != MotionType::Static);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::Instance::get()->bodyInterface;
	bodyInterface->MoveKinematic(body->GetID(), toVec3(position), toQuat(rotation), deltaTime);
}

void RigidbodyComponent::setWorldTransform(bool activate)
{
	GARDEN_ASSERT(shape);
	auto transformView = TransformSystem::Instance::get()->getComponent(entity);
	auto model = transformView->calcModel();
	setPosAndRot(getTranslation(model), extractQuat(extractRotation(model)), activate);
}

//**********************************************************************************************************************
static JPH::RVec3 calcOtherPoint(ID<Entity> otherBody, f32x4 otherBodyPoint,
	f32x4 thisBodyPoint, RigidbodyComponent* thisView) noexcept
{
	if (otherBody)
		return toRVec3(otherBodyPoint);
	else
		return toRVec3(thisView->getPosition() + thisView->getRotation() * (otherBodyPoint + thisBodyPoint));
}

void RigidbodyComponent::createConstraint(ID<Entity> otherBody, 
	ConstraintType type, f32x4 thisBodyPoint, f32x4 otherBodyPoint)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(otherBody != entity);
	View<RigidbodyComponent> otherRigidbodyView;
	JPH::Body* targetBody;

	if (otherBody)
	{
		otherRigidbodyView = PhysicsSystem::Instance::get()->getComponent(otherBody);
		GARDEN_ASSERT(otherRigidbodyView->shape);
		targetBody = (JPH::Body*)otherRigidbodyView->instance;
	}
	else
	{
		targetBody = &JPH::Body::sFixedToWorld;
	}

	auto thisBody = (JPH::Body*)this->instance;
	JPH::TwoBodyConstraint* instance = nullptr;

	if (type == ConstraintType::Fixed)
	{
		JPH::FixedConstraintSettings settings;
		if (thisBodyPoint == f32x4::max || otherBodyPoint == f32x4::max)
		{
			if (otherBody)
				settings.mAutoDetectPoint = true;
			else
				settings.mPoint2 = toRVec3(getPosition());
		}
		else
		{
			settings.mPoint1 = toRVec3(thisBodyPoint);
			settings.mPoint2 = calcOtherPoint(otherBody, otherBodyPoint, thisBodyPoint, this);
		}
		instance = settings.Create(*thisBody, *targetBody);
	}
	else if (type == ConstraintType::Point)
	{
		JPH::PointConstraintSettings settings;
		if (thisBodyPoint == f32x4::max || otherBodyPoint == f32x4::max)
		{
			if (!otherBody)
				settings.mPoint2 = toRVec3(getPosition());
		}
		else
		{
			settings.mPoint1 = toRVec3(thisBodyPoint);
			settings.mPoint2 = calcOtherPoint(otherBody, otherBodyPoint, thisBodyPoint, this);
		}
		instance = settings.Create(*thisBody, *targetBody);
	}
	else abort();
	
	instance->AddRef();
	auto physicsInstance = (JPH::PhysicsSystem*)PhysicsSystem::Instance::get()->physicsInstance;
	physicsInstance->AddConstraint(instance);

	Constraint constraint;
	constraint.instance = instance;
	constraint.otherBody = otherBody;
	constraint.type = type;
	constraints.push_back(constraint);

	if (otherBody)
	{
		constraint.otherBody = entity;
		otherRigidbodyView->constraints.push_back(constraint);
	}
}

//**********************************************************************************************************************
void RigidbodyComponent::destroyConstraint(uint32 index)
{
	GARDEN_ASSERT(index < constraints.size());
	auto& constraint = constraints[index];

	if (constraint.otherBody)
	{
		auto rigidbodyView = PhysicsSystem::Instance::get()->getComponent(constraint.otherBody);
		auto& otherConstraints = rigidbodyView->constraints;
		for (auto i = otherConstraints.begin(); i != otherConstraints.end(); i++)
		{
			if (i->otherBody != entity)
				continue;
			otherConstraints.erase(i);
			break;
		}
	}

	auto instance = (JPH::Constraint*)constraint.instance;
	constraints.erase(constraints.begin() + index);
	auto physicsInstance = (JPH::PhysicsSystem*)PhysicsSystem::Instance::get()->physicsInstance;
	physicsInstance->RemoveConstraint(instance);
	instance->Release();
}
void RigidbodyComponent::destroyAllConstraints()
{
	auto physicsSystem = PhysicsSystem::Instance::get();
	auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
	auto hasEntities = Manager::Instance::get()->getEntities().getCount() > 0; // Note: Detecting termination cleanup

	for (auto i = constraints.rbegin(); i != constraints.rend(); i++)
	{
		if (i->otherBody && hasEntities)
		{
			auto rigidbodyView = physicsSystem->getComponent(i->otherBody);
			auto& otherConstraints = rigidbodyView->constraints;
			for (int64 j = (int64)otherConstraints.size() - 1; j >= 0; j--) // TODO: suboptimal. Maybe optimize?
			{
				if (otherConstraints[j].otherBody != entity)
					continue;
				otherConstraints.erase(otherConstraints.begin() + j);
				break;
			}
		}

		auto instance = (JPH::Constraint*)i->instance;
		physicsInstance->RemoveConstraint(instance);
		instance->Release();
	}
	constraints.clear();
}

bool RigidbodyComponent::isConstraintEnabled(uint32 index) const
{
	GARDEN_ASSERT(index < constraints.size());
	auto& constraint = constraints[index];
	auto instance = (const JPH::Constraint*)constraint.instance;
	return instance->GetEnabled();
}
void RigidbodyComponent::setConstraintEnabled(uint32 index, bool isEnabled)
{
	GARDEN_ASSERT(index < constraints.size());
	auto& constraint = constraints[index];
	auto instance = (JPH::Constraint*)constraint.instance;
	return instance->SetEnabled(isEnabled);
}

//**********************************************************************************************************************
PhysicsSystem::PhysicsSystem(const Properties& properties, bool setSingleton) : Singleton(setSingleton)
{
	GARDEN_ASSERT(properties.collisionLayerCount >= (uint16)CollisionLayer::DefaultCount);
	GARDEN_ASSERT(properties.broadPhaseLayerCount >= (uint16)BroadPhaseLayer::DefaultCount);

	this->properties = properties;

	auto manager = Manager::Instance::get();
	manager->registerEventBefore("Simulate", "Update");

	ECSM_SUBSCRIBE_TO_EVENT("PreInit", PhysicsSystem::preInit);
	ECSM_SUBSCRIBE_TO_EVENT("PostInit", PhysicsSystem::postInit);
	ECSM_SUBSCRIBE_TO_EVENT("Simulate", PhysicsSystem::simulate);
	
	// This needs to be done before any other Jolt function is called.
	JPH::RegisterDefaultAllocator(); 

	// Install trace and assert callbacks
	JPH::Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

	// Create a factory, this class is responsible for creating instances of classes 
	// based on their name or hash and is mainly used for deserialization of saved data.
	JPH::Factory::sInstance = new JPH::Factory();
}
PhysicsSystem::~PhysicsSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreInit", PhysicsSystem::preInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("PostInit", PhysicsSystem::postInit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Simulate", PhysicsSystem::simulate);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("Simulate");

		delete (GardenJobSystem*)jobSystem;
		delete (JPH::PhysicsSystem*)physicsInstance;
		delete (GardenContactListener*)contactListener;
		delete (GardenBodyActivationListener*)bodyListener;
		delete (JPH::ObjectVsBroadPhaseLayerFilterTable*)objVsBpLayerFilter;
		delete (JPH::BroadPhaseLayerInterfaceTable*)bpLayerInterface;
		delete (JPH::ObjectLayerPairFilterTable*)objVsObjLayerFilter;
		#ifdef JPH_DISABLE_TEMP_ALLOCATOR
		delete (JPH::TempAllocatorMalloc*)tempAllocator;
		#else
		delete (JPH::TempAllocatorImpl*)tempAllocator;
		#endif	
		delete JPH::Factory::sInstance;
	}
	else
	{
		shapes.clear(false);
	}

	JPH::UnregisterTypes();
	JPH::Factory::sInstance = nullptr;
	unsetSingleton();
}	

//**********************************************************************************************************************
void PhysicsSystem::preInit()
{
	// Note: You should register all custom physics shapes before this function call!

	// Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
	JPH::RegisterTypes();

	// We need a temp allocator for temporary allocations during the physics update.
	#ifdef JPH_DISABLE_TEMP_ALLOCATOR
	auto tempAllocator = new JPH::TempAllocatorMalloc();
	#else
	auto tempAllocator = new JPH::TempAllocatorImpl(properties.tempBufferSize);
	#endif	
	this->tempAllocator = tempAllocator;

	// Create class that filters object vs object layers
	auto objVsObjLayerFilter = new JPH::ObjectLayerPairFilterTable(properties.collisionLayerCount);
	this->objVsObjLayerFilter = objVsObjLayerFilter;
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::Moving, (uint16)CollisionLayer::NonMoving);
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::Moving, (uint16)CollisionLayer::Moving);
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::Moving, (uint16)CollisionLayer::Sensor);
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::LqDebris, (uint16)CollisionLayer::NonMoving);
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::HqDebris, (uint16)CollisionLayer::NonMoving);
	objVsObjLayerFilter->EnableCollision((uint16)CollisionLayer::HqDebris, (uint16)CollisionLayer::Moving);

	// Create mapping table from object layer to broadphase layer
	auto bpLayerInterface = new JPH::BroadPhaseLayerInterfaceTable(
		properties.collisionLayerCount, properties.broadPhaseLayerCount);
	this->bpLayerInterface = bpLayerInterface;
	bpLayerInterface->MapObjectToBroadPhaseLayer((uint16)CollisionLayer::NonMoving,
		JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::NonMoving));
	bpLayerInterface->MapObjectToBroadPhaseLayer((uint16)CollisionLayer::Moving,
		JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::Moving));
	bpLayerInterface->MapObjectToBroadPhaseLayer((uint16)CollisionLayer::Sensor,
		JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::Sensor));
	bpLayerInterface->MapObjectToBroadPhaseLayer((uint16)CollisionLayer::HqDebris,
		JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::Moving));
	bpLayerInterface->MapObjectToBroadPhaseLayer((uint16)CollisionLayer::LqDebris,
		JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::LqDebris));

	#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	bpLayerInterface->SetBroadPhaseLayerName(JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::NonMoving), "NonMoving");
	bpLayerInterface->SetBroadPhaseLayerName(JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::Moving), "Moving");
	bpLayerInterface->SetBroadPhaseLayerName(JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::Sensor), "Sensor");
	bpLayerInterface->SetBroadPhaseLayerName(JPH::BroadPhaseLayer((uint8)BroadPhaseLayer::LqDebris), "LqDebris");
	#endif

	// Create class that filters object vs broadphase layers
	auto objVsBpLayerFilter = new JPH::ObjectVsBroadPhaseLayerFilterTable(*bpLayerInterface, 
		properties.broadPhaseLayerCount, *objVsObjLayerFilter, properties.collisionLayerCount);
	this->objVsBpLayerFilter = objVsBpLayerFilter;

	auto physicsInstance = new JPH::PhysicsSystem();
	this->physicsInstance = physicsInstance;

	physicsInstance->Init(properties.maxRigidbodyCount, properties.bodyMutexCount, properties.maxBodyPairCount,
		properties.maxContactConstraintCount, *bpLayerInterface, *objVsBpLayerFilter, *objVsObjLayerFilter);

	// A body activation listener gets notified when bodies activate and go to sleep
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	auto bodyListener = new GardenBodyActivationListener(this, &bodyEvents, &bodyEventLocker);
	this->bodyListener = bodyListener;
	physicsInstance->SetBodyActivationListener(bodyListener);

	// A contact listener gets notified when bodies (are about to) collide, and when they separate again.
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	auto contactListener = new GardenContactListener(this, &bodyEvents, &bodyEventLocker);
	this->contactListener = contactListener;
	physicsInstance->SetContactListener(contactListener);

	bodyInterface = &physicsInstance->GetBodyInterfaceNoLock(); // Version that does not lock the bodies, use with great care!
	lockInterface = &physicsInstance->GetBodyLockInterfaceNoLock(); // Version that does not lock the bodies, use with great care!
	narrowPhaseQuery = &physicsInstance->GetNarrowPhaseQueryNoLock(); // Version that does not lock the bodies, use with great care!

	// We need a job system that will execute physics jobs on multiple threads.
	auto threadPool = &ThreadSystem::Instance::get()->getForegroundPool();
	auto jobSystem = new GardenJobSystem(threadPool);
	this->jobSystem = jobSystem;
}
void PhysicsSystem::postInit()
{
	optimizeBroadPhase();
}

//**********************************************************************************************************************
void PhysicsSystem::prepareSimulate()
{
	SET_CPU_ZONE_SCOPED("Physics Simulate Prepare");

	if (components.getCount() > 0 && TransformSystem::Instance::has())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems([componentData](const ThreadPool::Task& task)
		{
			auto physicsInstance = (JPH::PhysicsSystem*)PhysicsSystem::Instance::get()->physicsInstance;
			auto& bodyInterface = physicsInstance->GetBodyInterface(); // This one is with lock.
			auto transformSystem = TransformSystem::Instance::get();
			auto characterSystem = CharacterSystem::Instance::tryGet();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->entity || !rigidbodyView->instance || !rigidbodyView->inSimulation ||
					rigidbodyView->getMotionType() == MotionType::Static ||
					(characterSystem && characterSystem->hasComponent(rigidbodyView->entity)))
				{
					continue;
				}

				auto transformView = transformSystem->tryGetComponent(rigidbodyView->entity);
				if (!transformView || !transformView->isActive())
					continue;

				auto body = (JPH::Body*)rigidbodyView->instance;
				JPH::RVec3 position = {}; JPH::Quat rotation = {};
				bodyInterface.GetPositionAndRotation(body->GetID(), position, rotation);
				transformView->setPosition(rigidbodyView->lastPosition = toF32x4(position));
				transformView->setRotation(rigidbodyView->lastRotation = toQuat(rotation));
			}
		},
		components.getOccupancy());
		threadPool->wait();
	}
}

//**********************************************************************************************************************
static void toEventName(string& eventName, string_view eventListener, BodyEvent eventType)
{
	eventName.assign(eventListener);
	switch (eventType)
	{
	case BodyEvent::Activated:
		eventName += ".Activated";
		break;
	case BodyEvent::Deactivated:
		eventName += ".Deactivated";
		break;
	case BodyEvent::Entered:
		eventName += ".Entered";
		break;
	case BodyEvent::Stayed:
		eventName += ".Stayed";
		break;
	case BodyEvent::Exited:
		eventName += ".Exited";
		break;
	default: abort();
	}
}

void PhysicsSystem::processSimulate()
{
	SET_CPU_ZONE_SCOPED("Physics Simulate Process");

	auto manager = Manager::Instance::get();
	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);
	string eventName;

	for (auto& bodyEvent : bodyEvents)
	{
		ID<Entity> entity1 = {}, entity2 = {};
		if (bodyEvent.eventType == BodyEvent::Exited)
		{
			{
				JPH::BodyLockRead lock(lockInterface, JPH::BodyID(bodyEvent.data1));
				if (lock.Succeeded())
					*entity1 = (uint32)lock.GetBody().GetUserData();
			}
			{
				JPH::BodyLockRead lock(lockInterface, JPH::BodyID(bodyEvent.data2));
				if (lock.Succeeded())
					*entity2 = (uint32)lock.GetBody().GetUserData();
			}
		}
		else
		{
			*entity1 = bodyEvent.data1;
			*entity2 = bodyEvent.data2;
		}

		if (entity1)
		{
			auto rigidbodyView = getComponent(entity1);
			toEventName(eventName, rigidbodyView->eventListener, bodyEvent.eventType);
			thisBody = entity1; otherBody = entity2;
			manager->tryRunEvent(eventName);
		}
		if (entity2)
		{
			auto rigidbodyView = getComponent(entity2);
			toEventName(eventName, rigidbodyView->eventListener, bodyEvent.eventType);
			thisBody = entity2; otherBody = entity1;
			manager->tryRunEvent(eventName);
		}
	}

	thisBody = otherBody = {};
	bodyEvents.clear();
}

//**********************************************************************************************************************
void PhysicsSystem::interpolateResult(float t)
{
	SET_CPU_ZONE_SCOPED("Physics Result Interpolate");

	if (components.getCount() > 0 && TransformSystem::Instance::has())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems([componentData, t](const ThreadPool::Task& task)
		{
			auto characterSystem = CharacterSystem::Instance::tryGet();
			auto transformSystem = TransformSystem::Instance::get();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->instance || !rigidbodyView->inSimulation || 
					rigidbodyView->getMotionType() == MotionType::Static ||
					(characterSystem && characterSystem->hasComponent(rigidbodyView->entity)))
				{
					continue;
				}

				auto transformView = transformSystem->tryGetComponent(rigidbodyView->entity);
				if (!transformView)
					continue;

				f32x4 position; quat rotation;
				rigidbodyView->getPosAndRot(position, rotation);
				transformView->setPosition(lerp(rigidbodyView->lastPosition, position, t));
				transformView->setRotation(slerp(rigidbodyView->lastRotation, rotation, t));
			}
		},
		components.getOccupancy());
		threadPool->wait();
	}
}

void PhysicsSystem::simulate()
{
	SET_CPU_ZONE_SCOPED("Physics Simulate");

	auto inputSystem = InputSystem::Instance::get();
	auto deltaTime = (float)inputSystem->getDeltaTime();
	auto simDeltaTime = 1.0f / (float)(simulationRate + 1);
	deltaTimeAccum += deltaTime;

	if (deltaTimeAccum >= simDeltaTime)
	{
		prepareSimulate();

		auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
		auto tempAllocator = (JPH::TempAllocator*)this->tempAllocator;
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto stepCount = (uint32)(deltaTimeAccum / simDeltaTime);

		if (cascadeLagCount > simulationRate * cascadeLagThreshold)
		{
			// Note: Trying to recover from a cascade chain lag. (snowball effect)
			stepCount = 1;
			cascadeLagCount = 0;
		}
		else
		{
			if (stepCount > 1)
				cascadeLagCount++;
			else
				cascadeLagCount = 0;
			deltaTimeAccum /= (float)stepCount;
		}

		for (uint32 i = 0; i < stepCount; i++)
		{
			physicsInstance->Update(deltaTimeAccum, collisionSteps, tempAllocator, jobSystem);

			#ifndef JPH_DISABLE_TEMP_ALLOCATOR
			GARDEN_ASSERT(static_cast<JPH::TempAllocatorImpl*>(tempAllocator)->IsEmpty());
			#endif
		}

		#if (GARDEN_DEBUG || GARDEN_EDITOR) && defined(JPH_TRACK_BROADPHASE_STATS)
		static auto lastBroadphaseTime = 0.0;
		if (logBroadPhaseStats && lastBroadphaseTime < inputSystem->getCurrentTime())
		{
			physicsInstance->ReportBroadphaseStats();
			lastBroadphaseTime = inputSystem->getCurrentTime() + statsLogRate;
		}
		#endif

		#if (GARDEN_DEBUG || GARDEN_EDITOR) && defined(JPH_TRACK_NARROWPHASE_STATS)
		static auto lastNarrowphaseTime = 0.0;
		if (logNarrowPhaseStats && lastNarrowphaseTime < inputSystem->getCurrentTime())
		{
			JPH::NarrowPhaseStat::sReportStats();
			lastNarrowphaseTime = inputSystem->getCurrentTime() + statsLogRate;
		}
		#endif

		processSimulate();
		deltaTimeAccum = 0.0f;
	}
	else
	{
		auto t = clamp(deltaTimeAccum / simDeltaTime, 0.0f, 1.0f);
		interpolateResult(t);
	}
}

//**********************************************************************************************************************
void PhysicsSystem::resetComponent(View<Component> component, bool full)
{
	auto rigidbodyView = View<RigidbodyComponent>(component);
	rigidbodyView->setShape({});
	rigidbodyView->destroyAllConstraints();

	if (full)
	{
		rigidbodyView->lastPosition = f32x4::zero;
		rigidbodyView->lastRotation = quat::identity;
		rigidbodyView->uid = 0;
		rigidbodyView->inSimulation = true;
		rigidbodyView->eventListener = "";
	}
}
void PhysicsSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<RigidbodyComponent>(source);
	auto destinationView = View<RigidbodyComponent>(destination);
	destinationView->inSimulation = sourceView->inSimulation;

	auto isActive = sourceView->isActive();
	auto motionType = sourceView->getMotionType();
	destinationView->setShape(sourceView->shape, motionType, sourceView->getCollisionLayer(), 
		isActive, sourceView->canBeKinematicOrDynamic(), sourceView->getAllowedDOF());
	destinationView->setSensor(sourceView->isSensor());
	destinationView->setKinematicVsStatic(sourceView->isKinematicVsStatic());

	f32x4 position; quat rotation;
	sourceView->getPosAndRot(position, rotation);
	destinationView->setPosAndRot(position, rotation, isActive);

	if (motionType != MotionType::Static)
	{
		destinationView->setLinearVelocity(sourceView->getLinearVelocity());
		destinationView->setAngularVelocity(sourceView->getAngularVelocity());
	}

	destinationView->lastPosition = sourceView->lastPosition;
	destinationView->lastRotation = sourceView->lastRotation;
	destinationView->eventListener = sourceView->eventListener;
	destinationView->uid = 0;
}
string_view PhysicsSystem::getComponentName() const
{
	return "Rigidbody";
}
void PhysicsSystem::disposeComponents()
{
	components.dispose();
	shapes.dispose();
}

//**********************************************************************************************************************
static void serializeShape(ISerializer& serializer, View<Shape> shapeView, bool isInner)
{
	auto subType = shapeView->getSubType();
	if (subType == ShapeSubType::Box)
	{
		auto halfExtent = shapeView->getBoxHalfExtent();
		if (halfExtent != f32x4(0.5f))
			serializer.write(isInner ? "innerHalfExtent" : "halfExtent", (float3)halfExtent);
		auto convexRadius = shapeView->getBoxConvexRadius();
		if (convexRadius != 0.05f)
			serializer.write(isInner ? "innerConvexRadius" : "convexRadius", convexRadius);
		serializer.write(isInner ? "innerShapeType" : "shapeType", string_view("Box"));
	}
}
void PhysicsSystem::serializeDecoratedShape(ISerializer& serializer, ID<Shape> shape)
{
	auto shapeView = shapes.get(shape);
	auto subType = shapeView->getSubType();
	if (subType == ShapeSubType::RotatedTranslated)
	{
		f32x4 position; quat rotation;
		shapeView->getPosAndRot(position, rotation);
		if (position != f32x4::zero)
			serializer.write("shapePosition", (float3)position);
		if (rotation != quat::identity)
			serializer.write("shapeRotation", rotation);
		serializer.write("shapeType", string_view("RotatedTranslated"));
		auto innerShape = shapeView->getInnerShape();
		serializeShape(serializer, shapes.get(innerShape), true);
	}
	else
	{
		serializeShape(serializer, shapeView, false);
	}
}

//**********************************************************************************************************************
void PhysicsSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto rigidbodyView = View<RigidbodyComponent>(component);

	if (!rigidbodyView->uid)
	{
		auto& randomDevice = serializer.randomDevice;
		uint32 uid[2] { randomDevice(), randomDevice() };
		rigidbodyView->uid = *(uint64*)(uid);
		GARDEN_ASSERT_MSG(rigidbodyView->uid, "Detected random device anomaly");
	}

	#if GARDEN_DEBUG
	auto emplaceResult = serializedEntities.emplace(rigidbodyView->uid);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected several entities with the same UID");
	#endif

	encodeBase64(valueStringCache, &rigidbodyView->uid, sizeof(uint64));
	valueStringCache.resize(valueStringCache.length() - 1);
	serializer.write("uid", valueStringCache);
	
	auto motionType = rigidbodyView->getMotionType();
	if (motionType == MotionType::Kinematic)
		serializer.write("motionType", string_view("Kinematic"));
	else if (motionType == MotionType::Dynamic)
		serializer.write("motionType", string_view("Dynamic"));

	if (!rigidbodyView->eventListener.empty())
		serializer.write("eventListener", rigidbodyView->eventListener);

	if (rigidbodyView->shape)
	{
		if (rigidbodyView->isActive())
			serializer.write("isActive", true);
		if (rigidbodyView->canBeKinematicOrDynamic())
			serializer.write("allowDynamicOrKinematic", true);
		if (rigidbodyView->isSensor())
			serializer.write("isSensor", true);
		if (rigidbodyView->isKinematicVsStatic())
			serializer.write("isKinematicVsStatic", true);
		serializer.write("collisionLayer", rigidbodyView->getCollisionLayer());

		f32x4 position; quat rotation;
		rigidbodyView->getPosAndRot(position, rotation);
		if (position != f32x4::zero)
			serializer.write("position", (float3)position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

		auto velocity = rigidbodyView->getLinearVelocity();
		if (velocity != f32x4::zero)
			serializer.write("linearVelocity", (float3)position);
		velocity = rigidbodyView->getAngularVelocity();
		if (velocity != f32x4::zero)
			serializer.write("angularVelocity",(float3) position);

		if (motionType != MotionType::Static && rigidbodyView->getAllowedDOF() != AllowedDOF::All)
		{
			auto allowedDOF = rigidbodyView->getAllowedDOF();
			if (!hasAnyFlag(allowedDOF, AllowedDOF::TranslationX))
				serializer.write("allowedDofTransX", false);
			if (!hasAnyFlag(allowedDOF, AllowedDOF::TranslationY))
				serializer.write("allowedDofTransY", false);
			if (!hasAnyFlag(allowedDOF, AllowedDOF::TranslationZ))
				serializer.write("allowedDofTransZ", false);
			if (!hasAnyFlag(allowedDOF, AllowedDOF::RotationX))
				serializer.write("allowedDofRotX", false);
			if (!hasAnyFlag(allowedDOF, AllowedDOF::RotationY))
				serializer.write("allowedDofRotY", false);
			if (!hasAnyFlag(allowedDOF, AllowedDOF::RotationZ))
				serializer.write("allowedDofRotZ", false);
		}

		serializeDecoratedShape(serializer, rigidbodyView->shape);
	}

	if (!rigidbodyView->constraints.empty())
	{
		serializer.beginChild("constraints");
		const auto& constraints = rigidbodyView->constraints;
		for (auto& constraint : constraints)
		{
			auto searchResult = serializedConstraints.find(constraint.otherBody);
			if (searchResult != serializedConstraints.end())
				continue;

			serializer.beginArrayElement();

			switch (constraint.type)
			{
			case ConstraintType::Point:
				serializer.write("type", string_view("Point"));
				break;
			default:
				break;
			}

			auto constraintView = getComponent(constraint.otherBody);
			encodeBase64(valueStringCache, &constraintView->uid, sizeof(uint64));
			valueStringCache.resize(valueStringCache.length() - 1);
			serializer.write("uid", valueStringCache);

			serializer.endArrayElement();
		}
		serializer.endChild();

		auto emplaceResult = serializedConstraints.emplace(rigidbodyView->entity);
		GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	}
}
void PhysicsSystem::postSerialize(ISerializer& serializer)
{
	serializedConstraints.clear();
	#if GARDEN_DEBUG
	serializedEntities.clear();
	#endif
}

//**********************************************************************************************************************
static ID<Shape> deserializeShape(IDeserializer& deserializer, string_view shapeType, bool isInner)
{
	if (shapeType == "Box")
	{
		f32x4 halfExtent(0.5f); float convexRadius = 0.05f;
		deserializer.read(isInner ? "innerHalfExtent" : "halfExtent", halfExtent, 3);
		deserializer.read(isInner ? "innerConvexRadius" : "convexRadius", convexRadius);
		return PhysicsSystem::Instance::get()->createSharedBoxShape(halfExtent, convexRadius);
	}
	return {};
}
ID<Shape> PhysicsSystem::deserializeDecoratedShape(IDeserializer& deserializer, string& valueStringCache)
{
	if (valueStringCache == "RotatedTranslated")
	{
		if (!deserializer.read("innerShapeType", valueStringCache))
			return {};
		auto innerShape = deserializeShape(deserializer, valueStringCache, true);
		if (!innerShape)
			return {};
		auto position = f32x4::zero;
		deserializer.read("shapePosition", position, 3);
		auto rotation = quat::identity;
		deserializer.read("shapeRotation", rotation);
		return PhysicsSystem::Instance::get()->createSharedRotTransShape(innerShape, position, rotation);
	}
	return deserializeShape(deserializer, valueStringCache, false);
}

//**********************************************************************************************************************
void PhysicsSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto rigidbodyView = View<RigidbodyComponent>(component);

	if (deserializer.read("uid", valueStringCache) &&
		valueStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)))
	{
		if (decodeBase64(&rigidbodyView->uid, valueStringCache, ModpDecodePolicy::kForgiving))
		{
			auto result = deserializedEntities.emplace(rigidbodyView->uid, rigidbodyView->entity);
			if (!result.second)
				GARDEN_LOG_ERROR("Deserialized entity with already existing UID. (uid: " + valueStringCache + ")");
		}
	}

	auto motionType = MotionType::Static;
	if (deserializer.read("motionType", valueStringCache))
	{
		if (valueStringCache == "Kinematic")
			motionType = MotionType::Kinematic;
		else if (valueStringCache == "Dynamic")
			motionType = MotionType::Dynamic;
	}

	deserializer.read("eventListener", rigidbodyView->eventListener);

	if (deserializer.read("shapeType", valueStringCache))
	{
		auto shape = deserializeDecoratedShape(deserializer, valueStringCache);
		if (shape)
		{
			auto isActive = false, allowDynamicOrKinematic = false;
			deserializer.read("isActive", isActive);
			deserializer.read("allowDynamicOrKinematic", allowDynamicOrKinematic);

			int32 collisionLayer = -1;
			deserializer.read("collisionLayer", collisionLayer);

			auto allowedDOF = AllowedDOF::All;
			auto allowedDofValue = true;
			if (deserializer.read("allowedDofTransX", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::TranslationX; }
			if (deserializer.read("allowedDofTransY", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::TranslationY; }
			if (deserializer.read("allowedDofTransZ", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::TranslationZ; }
			if (deserializer.read("allowedDofRotX", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::RotationX; }
			if (deserializer.read("allowedDofRotY", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::RotationY; }
			if (deserializer.read("allowedDofRotZ", allowedDofValue))
			{ if (!allowedDofValue) allowedDOF &= ~AllowedDOF::RotationZ; }

			rigidbodyView->setShape(shape, motionType, collisionLayer,
				isActive, allowDynamicOrKinematic, allowedDOF);

			auto boolValue = false;
			if (deserializer.read("isSensor", boolValue))
				rigidbodyView->setSensor(boolValue);
			if (deserializer.read("isKinematicVsStatic", boolValue))
				rigidbodyView->setKinematicVsStatic(boolValue);

			auto f32x4Value = f32x4::zero; auto rotation = quat::identity;
			deserializer.read("position", f32x4Value, 3);
			deserializer.read("rotation", rotation);
			if (f32x4Value != f32x4::zero || rotation != quat::identity)
				rigidbodyView->setPosAndRot(f32x4Value, rotation, isActive);

			f32x4Value = f32x4::zero;
			deserializer.read("linearVelocity", f32x4Value, 3);
			if (f32x4Value != f32x4::zero)
				rigidbodyView->setLinearVelocity(f32x4Value);
			f32x4Value = f32x4::zero;
			deserializer.read("angularVelocity", f32x4Value, 3);
			if (f32x4Value != f32x4::zero)
				rigidbodyView->setAngularVelocity(f32x4Value);
		}
	}

	if (deserializer.beginChild("constraints"))
	{
		auto constraintCount = (uint32)deserializer.getArraySize();

		for (uint32 i = 0; i < constraintCount; i++)
		{
			deserializer.beginArrayElement(i);
			if (deserializer.read("uid", valueStringCache))
			{
				EntityConstraint entityConstraint = {};
				entityConstraint.entity = rigidbodyView->entity;

				if (deserializer.read("type", valueStringCache))
				{
					if (valueStringCache == "Point")
						entityConstraint.type = ConstraintType::Point;
				}

				if (valueStringCache.empty())
				{
					deserializedConstraints.push_back(entityConstraint);
				}
				else if (valueStringCache.size() + 1 == modp_b64_encode_data_len(sizeof(uint64)) && 
					decodeBase64(&entityConstraint.otherUID, valueStringCache, ModpDecodePolicy::kForgiving))
				{
					if (rigidbodyView->uid == entityConstraint.otherUID)
					{
						GARDEN_LOG_ERROR("Deserialized entity with the same constraint UID. ("
							"uid: " + valueStringCache + ")");
					}
					else
					{
						deserializedConstraints.push_back(entityConstraint);
					}
				}
			}
			deserializer.endArrayElement();
		}
	}
}
void PhysicsSystem::postDeserialize(IDeserializer& deserializer)
{
	for (auto thisConstraint : deserializedConstraints)
	{
		auto otherConstraint = deserializedEntities.find(thisConstraint.otherUID);
		if (otherConstraint == deserializedEntities.end())
		{
			encodeBase64(valueStringCache, &thisConstraint.otherUID, sizeof(uint64));
			valueStringCache.resize(valueStringCache.length() - 1);
			GARDEN_LOG_ERROR("Deserialized entity constraint does not exist. ("
				"constraintUID: " + valueStringCache + ")");
			continue;
		}

		auto rigidbodyView = getComponent(thisConstraint.entity);
		rigidbodyView->createConstraint(otherConstraint->second, thisConstraint.type);
	}

	deserializedConstraints.clear();
	deserializedEntities.clear();
}

//**********************************************************************************************************************
f32x4 PhysicsSystem::getGravity() const noexcept
{
	auto physicsInstance = (const JPH::PhysicsSystem*)this->physicsInstance;
	return toF32x4(physicsInstance->GetGravity());
}
void PhysicsSystem::setGravity(f32x4 gravity) const noexcept
{
	auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
	physicsInstance->SetGravity(toVec3(gravity));
}

ID<Shape> PhysicsSystem::createEmptyShape(f32x4 centerOfMass)
{
	auto emptyShape = new JPH::EmptyShape(toVec3(centerOfMass));
	auto instance = shapes.create(emptyShape);
	emptyShape->SetUserData((JPH::uint64)*instance);
	return instance;
}
ID<Shape> PhysicsSystem::createSharedEmptyShape(f32x4 centerOfMass)
{
	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &centerOfMass, sizeof(float3));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedEmptyShapes.find(hash);
	if (searchResult != sharedEmptyShapes.end())
		return searchResult->second;

	auto instance = createEmptyShape(centerOfMass);
	auto emplaceResult = sharedEmptyShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createBoxShape(f32x4 halfExtent, float convexRadius, float density)
{
	GARDEN_ASSERT(areAllTrue(halfExtent >= convexRadius));
	GARDEN_ASSERT(convexRadius >= 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto boxShape = new JPH::BoxShape(toVec3(halfExtent), convexRadius);
	boxShape->SetDensity(density);
	boxShape->AddRef();
	auto instance = shapes.create(boxShape);
	boxShape->SetUserData((JPH::uint64)*instance);
	return instance;
}

ID<Shape> PhysicsSystem::createSharedBoxShape(f32x4 halfExtent, float convexRadius, float density)
{
	GARDEN_ASSERT(areAllTrue(halfExtent >= convexRadius));
	GARDEN_ASSERT(convexRadius >= 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &halfExtent, sizeof(float3));
	Hash128::updateState(hashState, &convexRadius, sizeof(float));
	Hash128::updateState(hashState, &density, sizeof(float));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedBoxShapes.find(hash);
	if (searchResult != sharedBoxShapes.end())
		return searchResult->second;

	auto instance = createBoxShape(halfExtent, convexRadius, density);
	auto emplaceResult = sharedBoxShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createSphereShape(float radius, float density)
{
	GARDEN_ASSERT(radius > 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto sphereShape = new JPH::SphereShape(radius);
	sphereShape->SetDensity(density);
	sphereShape->AddRef();
	auto instance = shapes.create(sphereShape);
	sphereShape->SetUserData((JPH::uint64)*instance);
	return instance;
}

ID<Shape> PhysicsSystem::createSharedSphereShape(float radius, float density)
{
	GARDEN_ASSERT(radius > 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &radius, sizeof(float));
	Hash128::updateState(hashState, &density, sizeof(float));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedSphereShapes.find(hash);
	if (searchResult != sharedSphereShapes.end())
		return searchResult->second;

	auto instance = createSphereShape(radius, density);
	auto emplaceResult = sharedSphereShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createCapsuleShape(float halfheight, float radius, float density)
{
	GARDEN_ASSERT(halfheight > 0.0f);
	GARDEN_ASSERT(radius > 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto capsuleShape = new JPH::CapsuleShape(halfheight, radius);
	capsuleShape->SetDensity(density);
	capsuleShape->AddRef();
	auto instance = shapes.create(capsuleShape);
	capsuleShape->SetUserData((JPH::uint64)*instance);
	return instance;
}

ID<Shape> PhysicsSystem::createSharedCapsuleShape(float halfheight, float radius, float density)
{
	GARDEN_ASSERT(halfheight > 0.0f);
	GARDEN_ASSERT(radius > 0.0f);
	GARDEN_ASSERT(density > 0.0f);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &halfheight, sizeof(float));
	Hash128::updateState(hashState, &radius, sizeof(float));
	Hash128::updateState(hashState, &density, sizeof(float));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedCapsuleShapes.find(hash);
	if (searchResult != sharedCapsuleShapes.end())
		return searchResult->second;

	auto instance = createCapsuleShape(halfheight, radius, density);
	auto emplaceResult = sharedCapsuleShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createRotTransShape(ID<Shape> innerShape, f32x4 position, quat rotation)
{
	GARDEN_ASSERT(innerShape);
	auto innerView = get(innerShape);
	auto rotTransShape = new JPH::RotatedTranslatedShape(toVec3(position),
		toQuat(rotation), (JPH::Shape*)innerView->instance);
	rotTransShape->AddRef();
	auto instance = shapes.create(rotTransShape);
	rotTransShape->SetUserData((JPH::uint64)*instance);
	return instance;
}
ID<Shape> PhysicsSystem::createSharedRotTransShape(ID<Shape> innerShape, f32x4 position, quat rotation)
{
	GARDEN_ASSERT(innerShape);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &innerShape, sizeof(ID<Shape>));
	Hash128::updateState(hashState, &position, sizeof(float3));
	Hash128::updateState(hashState, &rotation, sizeof(float3));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedRotTransShapes.find(hash);
	if (searchResult != sharedRotTransShapes.end())
		return searchResult->second;

	auto instance = createRotTransShape(innerShape, position, rotation);
	auto emplaceResult = sharedRotTransShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createCustomShape(void* shapeInstance)
{
	GARDEN_ASSERT(shapeInstance);
	auto customShape = (JPH::Shape*)shapeInstance;
	customShape->AddRef();
	auto instance = shapes.create(shapeInstance);
	customShape->SetUserData((JPH::uint64)*instance);
	return instance;
}
ID<Shape> PhysicsSystem::createSharedCustomShape(void* shapeInstance)
{
	GARDEN_ASSERT(shapeInstance);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, shapeInstance, sizeof(void*));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedCustomShapes.find(hash);
	if (searchResult != sharedCustomShapes.end())
		return searchResult->second;

	auto instance = createCustomShape(shapeInstance);
	auto emplaceResult = sharedCustomShapes.emplace(hash, instance);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
	return instance;
}

//**********************************************************************************************************************
void PhysicsSystem::destroyShared(ID<Shape> shape)
{
	if (!shape)
		return;

	auto shapeView = shapes.get(shape);
	if (shapeView->getRefCount() > 2)
		return;

	auto subType = shapeView->getSubType();
	if (subType == ShapeSubType::Empty)
	{
		for (auto i = sharedEmptyShapes.begin(); i != sharedEmptyShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedEmptyShapes.erase(i);
			break;
		}
	}
	else if (subType == ShapeSubType::Box)
	{
		for (auto i = sharedBoxShapes.begin(); i != sharedBoxShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedBoxShapes.erase(i);
			break;
		}
	}
	else if (subType == ShapeSubType::Sphere)
	{
		for (auto i = sharedSphereShapes.begin(); i != sharedSphereShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedSphereShapes.erase(i);
			break;
		}
	}
	else if (subType == ShapeSubType::Capsule)
	{
		for (auto i = sharedCapsuleShapes.begin(); i != sharedCapsuleShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedCapsuleShapes.erase(i);
			break;
		}
	}
	else if (subType == ShapeSubType::RotatedTranslated)
	{
		for (auto i = sharedRotTransShapes.begin(); i != sharedRotTransShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedRotTransShapes.erase(i);
			break;
		}
	}
	else
	{
		for (auto i = sharedCustomShapes.begin(); i != sharedCustomShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedCustomShapes.erase(i);
			break;
		}
	}

	if (shapeView->isLastRef())
		destroy(ID<Shape>(shape));
}

void PhysicsSystem::optimizeBroadPhase()
{
	SET_CPU_ZONE_SCOPED("Broad Phase Optimize");
	auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
	physicsInstance->OptimizeBroadPhase();
}

//**********************************************************************************************************************
class ActiveBodyFilter final : public JPH::BodyFilter
{
	const JPH::BodyLockInterface* lockInterface = nullptr;
public:
	ActiveBodyFilter(const JPH::BodyLockInterface* lockInterface) : lockInterface(lockInterface) { }

	bool ShouldCollide(const JPH::BodyID &inBodyID) const final
	{
		JPH::BodyLockRead lock(*lockInterface, inBodyID);
		return lock.Succeeded() && lock.GetBody().IsInBroadPhase();
	}
};

static const JPH::BodyFilter defaultBodyFilter;

bool PhysicsSystem::castRay(const Ray& ray, RayCastHit& hit, float maxDistance, bool castInactive)
{
	GARDEN_ASSERT(ray.getDirection() != f32x4::zero);
	GARDEN_ASSERT(maxDistance > 0.0f);

	auto narrowPhaseQuery = (const JPH::NarrowPhaseQuery*)this->narrowPhaseQuery;
	auto activeBodyFilter = ActiveBodyFilter((const JPH::BodyLockInterface*)this->lockInterface);
	auto rayCast = JPH::RRayCast(toRVec3(ray.origin), toRVec3(ray.getDirection()) * maxDistance);

	JPH::RayCastResult rayCastResult;
	if (!narrowPhaseQuery->CastRay(rayCast, rayCastResult, {}, {}, 
		castInactive ? defaultBodyFilter : activeBodyFilter))
	{
		return false;
	}

	hit.subShapeID = rayCastResult.mSubShapeID2.GetValue();
	hit.surfacePoint = toF32x4(rayCast.GetPointOnRay(rayCastResult.mFraction));

	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);
	JPH::BodyLockRead lock(lockInterface, rayCastResult.mBodyID);
	if (lock.Succeeded())
	{
		auto& body = lock.GetBody();
		hit.surfaceNormal = toF32x4(body.GetWorldSpaceSurfaceNormal(
			rayCastResult.mSubShapeID2, rayCast.GetPointOnRay(rayCastResult.mFraction)));
		*hit.entity = (uint32)body.GetUserData();
	}
	return true;
}
bool PhysicsSystem::castRay(const Ray& ray, vector<RayCastHit>& hits, float maxDistance, bool sortHits, bool castInactive)
{
	GARDEN_ASSERT(ray.getDirection() != f32x4::zero);
	GARDEN_ASSERT(maxDistance > 0.0f);

	auto narrowPhaseQuery = (const JPH::NarrowPhaseQuery*)this->narrowPhaseQuery;
	auto activeBodyFilter = ActiveBodyFilter((const JPH::BodyLockInterface*)this->lockInterface);
	auto rayCast = JPH::RRayCast(toRVec3(ray.origin), toRVec3(ray.getDirection()) * maxDistance);

	static const JPH::RayCastSettings settings;
	JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
	narrowPhaseQuery->CastRay(rayCast, settings, collector, {}, {}, 
		castInactive ? defaultBodyFilter : activeBodyFilter);
	if (!collector.HadHit())
		return false;
	
	if (sortHits)
		collector.Sort();
	
	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);
	auto& collectorHits = collector.mHits;
	hits.resize(collectorHits.size());

	for (psize i = 0; i < hits.size(); i++)
	{
		auto hit = hits[i];
		auto collectorHit = collectorHits[i];
		hit.subShapeID = collectorHit.mSubShapeID2.GetValue();
		hit.surfacePoint = toF32x4(rayCast.GetPointOnRay(collectorHit.mFraction));

		JPH::BodyLockRead lock(lockInterface, collectorHit.mBodyID);
		if (lock.Succeeded())
		{
			auto& body = lock.GetBody();
			hit.surfaceNormal = toF32x4(body.GetWorldSpaceSurfaceNormal(
				collectorHit.mSubShapeID2, rayCast.GetPointOnRay(collectorHit.mFraction)));
			*hit.entity = (uint32)body.GetUserData();
		}
	}
	return true;
}

//**********************************************************************************************************************
bool PhysicsSystem::collidePoint(f32x4 point, ShapeHit& hit, bool collideInactive)
{
	auto narrowPhaseQuery = (const JPH::NarrowPhaseQuery*)this->narrowPhaseQuery;
	auto activeBodyFilter = ActiveBodyFilter((const JPH::BodyLockInterface*)this->lockInterface);

	JPH::AnyHitCollisionCollector<JPH::CollidePointCollector> collector;
	narrowPhaseQuery->CollidePoint(toVec3(point), collector, {}, {}, 
		collideInactive ? defaultBodyFilter : activeBodyFilter);
	if (!collector.HadHit())
		return false;

	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);
	JPH::BodyLockRead lock(lockInterface, collector.mHit.mBodyID);
	if (lock.Succeeded())
		*hit.entity = (uint32)lock.GetBody().GetUserData();
	hit.subShapeID = collector.mHit.mSubShapeID2.GetValue();
	return true;
}
bool PhysicsSystem::collidePoint(f32x4 point, vector<ShapeHit>& hits, bool collideInactive)
{
	auto narrowPhaseQuery = (const JPH::NarrowPhaseQuery*)this->narrowPhaseQuery;
	auto activeBodyFilter = ActiveBodyFilter((const JPH::BodyLockInterface*)this->lockInterface);

	JPH::AllHitCollisionCollector<JPH::CollidePointCollector> collector;
	narrowPhaseQuery->CollidePoint(toVec3(point), collector, {}, {}, 
		collideInactive ? defaultBodyFilter : activeBodyFilter);
	if (!collector.HadHit())
		return false;
	
	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);
	auto& collectorHits = collector.mHits;
	hits.resize(collectorHits.size());

	for (psize i = 0; i < hits.size(); i++)
	{
		auto hit = hits[i];
		auto collectorHit = collectorHits[i];
		hit.subShapeID = collectorHit.mSubShapeID2.GetValue();

		JPH::BodyLockRead lock(lockInterface, collectorHit.mBodyID);
		if (lock.Succeeded())
			*hit.entity = (uint32)lock.GetBody().GetUserData();
	}
	return true;
}

//**********************************************************************************************************************
void PhysicsSystem::activateRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);

	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto rigidbodyView = tryGetComponent(entity);
		if (rigidbodyView)
			rigidbodyView->activate();

		auto transformView = transformSystem->tryGetComponent(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}

void PhysicsSystem::setWorldTransformRecursive(ID<Entity> entity, bool activate)
{
	GARDEN_ASSERT(entity);

	auto transformSystem = TransformSystem::Instance::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto rigidbodyView = tryGetComponent(entity);
		if (rigidbodyView && rigidbodyView->getShape())
			rigidbodyView->setWorldTransform(activate);

		auto transformView = transformSystem->tryGetComponent(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}

//**********************************************************************************************************************
void PhysicsSystem::enableCollision(uint16 collisionLayer1, uint16 collisionLayer2)
{
	GARDEN_ASSERT(collisionLayer1 < properties.collisionLayerCount);
	GARDEN_ASSERT(collisionLayer2 < properties.collisionLayerCount);
	auto objVsObjLayerFilter = (JPH::ObjectLayerPairFilterTable*)this->objVsObjLayerFilter;
	objVsObjLayerFilter->EnableCollision(collisionLayer1, collisionLayer2);
}
void PhysicsSystem::disableCollision(uint16 collisionLayer1, uint16 collisionLayer2)
{
	GARDEN_ASSERT(collisionLayer1 < properties.collisionLayerCount);
	GARDEN_ASSERT(collisionLayer2 < properties.collisionLayerCount);
	auto objVsObjLayerFilter = (JPH::ObjectLayerPairFilterTable*)this->objVsObjLayerFilter;
	objVsObjLayerFilter->DisableCollision(collisionLayer1, collisionLayer2);
}
bool PhysicsSystem::isCollisionEnabled(uint16 collisionLayer1, uint16 collisionLayer2) const
{
	GARDEN_ASSERT(collisionLayer1 < properties.collisionLayerCount);
	GARDEN_ASSERT(collisionLayer2 < properties.collisionLayerCount);
	auto objVsObjLayerFilter = (JPH::ObjectLayerPairFilterTable*)this->objVsObjLayerFilter;
	return objVsObjLayerFilter->ShouldCollide(collisionLayer1, collisionLayer2); // TODO: render layer selector in imgui.
}

void PhysicsSystem::mapLayers(uint16 collisionLayer, uint8 broadPhaseLayer)
{
	GARDEN_ASSERT(collisionLayer < properties.collisionLayerCount);
	GARDEN_ASSERT(broadPhaseLayer < properties.broadPhaseLayerCount);
	auto bpLayerInterface = (JPH::BroadPhaseLayerInterfaceTable*)this->bpLayerInterface;
	bpLayerInterface->MapObjectToBroadPhaseLayer(collisionLayer, JPH::BroadPhaseLayer(broadPhaseLayer));
}

#if GARDEN_DEBUG
void PhysicsSystem::setBroadPhaseLayerName(uint8 broadPhaseLayer, const string& debugName)
{
	GARDEN_ASSERT(broadPhaseLayer < properties.broadPhaseLayerCount);
	#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	auto bpLayerInterface = (JPH::BroadPhaseLayerInterfaceTable*)this->bpLayerInterface;
	bpLayerInterface->SetBroadPhaseLayerName(JPH::BroadPhaseLayer(broadPhaseLayer), debugName.c_str());
	#endif
}
#endif
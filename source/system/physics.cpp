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

#include "garden/system/physics.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/system/log.hpp"

#include "Jolt/Jolt.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Core/FixedSizeFreeList.h"
#include "Jolt/Core/JobSystemWithBarrier.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"

using namespace ecsm;
using namespace garden;

//**********************************************************************************************************************
static void TraceImpl(const char* inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);

	auto logSystem = Manager::get()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace(buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
	auto logSystem = Manager::get()->tryGet<LogSystem>();
	if (logSystem)
	{
		logSystem->trace(string(inFile) + ":" + to_string(inLine) + ": (" + 
			inExpression + ") " + (inMessage != nullptr ? inMessage : ""));
	}
	return true;
};
#endif // JPH_ENABLE_ASSERTS

//**********************************************************************************************************************
// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics 
// simulation but only if you do collision testing).
namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr JPH::uint NUM_LAYERS(2);
};

//**********************************************************************************************************************
/// Class that determines if two object layers can collide
class GardenObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const final
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING: // Non moving only collides with moving
			return inObject2 == Layers::MOVING; 
		case Layers::MOVING: // Moving collides with everything
			return true; 
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

/// Class that determines if an object layer can collide with a broadphase layer
class GardenObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const final
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// This defines a mapping between object and broadphase layers.
class GardenBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
	JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
public:
	GardenBroadPhaseLayerInterface()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	JPH::uint GetNumBroadPhaseLayers() const final
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}
	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const final
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}

	#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
	{
		switch ((JPH::BroadPhaseLayer::Type)inLayer)
		{
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
			return "NON_MOVING";
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
			return "MOVING";
		default:
			JPH_ASSERT(false); return "INVALID";
		}
	}
	#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED
};

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

		threadPool->addTask(ThreadPool::Task([inJob](const ThreadPool::Task& task)
		{
			inJob->Execute();
			inJob->Release();
		}));
	}
	void QueueJobs(Job** inJobs, JPH::uint inNumJobs) final
	{
		static thread_local vector<ThreadPool::Task> taskArray;
		taskArray.resize(inNumJobs);

		for (JPH::uint i = 0; i < inNumJobs; i++)
		{
			auto job = inJobs[i];

			// Add reference to job because we're adding the job to the queue
			job->AddRef();

			taskArray[i] = ThreadPool::Task([job](const ThreadPool::Task& task)
			{
				job->Execute();
				job->Release();
			});
		}

		threadPool->addTasks(taskArray);
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
	GardenContactListener(PhysicsSystem* _physicsSystem, 
		vector<PhysicsSystem::Event>* _bodyEvents, mutex* _bodyEventLocker) :
		physicsSystem(_physicsSystem), bodyEvents(_bodyEvents), bodyEventLocker(_bodyEventLocker) { }

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
		auto rigidbodyView1 = physicsSystem->get(entity1);
		auto rigidbodyView2 = physicsSystem->get(entity2);

		if (!rigidbodyView1->getListeners().empty() || !rigidbodyView2->getListeners().empty())
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
		addEvent((uint32)inBody1.GetUserData(), (uint32)inBody2.GetUserData(), BodyEvent::ContactAdded);
	}
	void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) final
	{
		addEvent((uint32)inBody1.GetUserData(), (uint32)inBody2.GetUserData(), BodyEvent::ContactPersisted);
	}
	void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) final
	{
		PhysicsSystem::Event bodyEvent;
		bodyEvent.eventType = BodyEvent::ContactRemoved;
		bodyEvent.data1 = inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber();
		bodyEvent.data2 = inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber();
		bodyEventLocker->lock();
		bodyEvents->push_back(bodyEvent);
		bodyEventLocker->unlock();
	}
};

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
		auto rigidbodyView = physicsSystem->get(entity);

		if (!rigidbodyView->getListeners().empty())
			addEvent((uint32)inBodyUserData, BodyEvent::Activated);
	}
	void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
		ID<Entity> entity; *entity = inBodyUserData;
		if (!entity)
			return;

		auto rigidbodyView = physicsSystem->get(entity);
		if (!rigidbodyView->getListeners().empty())
			addEvent((uint32)inBodyUserData, BodyEvent::Deactivated);
	}
};

//**********************************************************************************************************************
static float3 toFloat3(const JPH::Vec3& v) noexcept
{
	return float3(v.GetX(), v.GetY(), v.GetZ());
}
static quat toQuat(const JPH::Quat& q) noexcept
{
	return quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

static JPH::Vec3 toVec3(const float3& v) noexcept
{
	return JPH::Vec3(v.x, v.y, v.z);
}
static JPH::RVec3 toRVec3(const float3& v) noexcept
{
	return JPH::RVec3(v.x, v.y, v.z);
}
static JPH::Quat toQuat(const quat& q) noexcept
{
	return JPH::Quat(q.x, q.y, q.z, q.w);
}

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
		auto physicsSystem = PhysicsSystem::get();
		auto innerView = physicsSystem->get(innerShape);
		if (innerView->instance)
			physicsSystem->destroyShared(innerShape);
	}

	instance = nullptr;
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
	static_assert((uint32)ShapeSubType::UserConvex8 == (uint32)JPH::EShapeSubType::UserConvex8,
		"ShapeSubType does not map to the EShapeSubType");
	auto instance = (const JPH::Shape*)this->instance;
	return (ShapeSubType)instance->GetSubType();
}

//**********************************************************************************************************************
float3 Shape::getBoxHalfExtent() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (const JPH::BoxShape*)this->instance;
	return toFloat3(boxInstance->GetHalfExtent());
}
float Shape::getBoxConvexRadius() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (const JPH::BoxShape*)this->instance;
	return boxInstance->GetConvexRadius();
}

//**********************************************************************************************************************
ID<Shape> Shape::getInnerShape() const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetType() == JPH::EShapeType::Decorated);
	auto decoratedInstance = (const JPH::DecoratedShape*)this->instance;
	auto innerInstance = decoratedInstance->GetInnerShape();
	ID<Shape> id; *id = (uint32)innerInstance->GetUserData();
	return id;
}
void Shape::getPosAndRot(float3& position, quat& rotation) const
{
	GARDEN_ASSERT(instance);
	auto instance = (const JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::RotatedTranslated);
	auto rotTransInstance = (const JPH::RotatedTranslatedShape*)this->instance;
	position = toFloat3(rotTransInstance->GetPosition());
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
bool RigidbodyComponent::destroy()
{
	if (instance)
	{
		auto body = (JPH::Body*)instance;
		auto physicsSystem = PhysicsSystem::get();
		auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;

		if (inSimulation)
		{
			bodyInterface->SetUserData(body->GetID(), 0);
			bodyInterface->RemoveBody(body->GetID());
		}

		bodyInterface->DestroyBody(body->GetID());
		physicsSystem->destroyShared(shape);

		this->shape = {};
		this->instance = nullptr;
	}

	return true;
}

bool RigidbodyComponent::tryRemoveListener(type_index systemType, BodyEvent eventType)
{
	for (auto i = listeners.begin(); i != listeners.end(); i++)
	{
		if (i->systemType != systemType || i->eventType != eventType)
			continue;
		listeners.erase(i);
		return true;
	}
	return false;
}

void RigidbodyComponent::setMotionType(MotionType motionType, bool activate)
{
	if (this->motionType == motionType)
		return;

	if (instance)
	{
		if (this->motionType == MotionType::Static && motionType != MotionType::Static)
		{
			GARDEN_ASSERT(canBeKinematicOrDynamic());
		}

		auto body = (JPH::Body*)instance;
		auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
		bodyInterface->SetMotionType(body->GetID(), (JPH::EMotionType)motionType,
			activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		bodyInterface->SetObjectLayer(body->GetID(),
			motionType == MotionType::Static ? Layers::NON_MOVING : Layers::MOVING);
	}

	this->motionType = motionType;
}

//**********************************************************************************************************************
void RigidbodyComponent::setShape(ID<Shape> shape, bool activate, 
	bool allowDynamicOrKinematic, bool isSensor, AllowedDOF allowedDOF)
{
	if (this->shape == shape)
		return;

	auto body = (JPH::Body*)instance;
	auto physicsSystem = PhysicsSystem::get();
	auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;

	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);
		auto shapeInstance = (JPH::Shape*)shapeView->instance;

		if (instance)
		{
			bodyInterface->SetShape(body->GetID(), shapeInstance, true,
				activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		}
		else
		{
			auto position = float3(0.0f); auto rotation = quat::identity;
			auto transformView = Manager::get()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				position = this->lastPosition = transformView->position;
				rotation = this->lastRotation = transformView->rotation;
			}

			JPH::BodyCreationSettings settings(shapeInstance, 
				toRVec3(position), toQuat(rotation), (JPH::EMotionType)motionType,
				motionType == MotionType::Static ? Layers::NON_MOVING : Layers::MOVING);
			settings.mUserData = *entity;
			settings.mAllowDynamicOrKinematic = allowDynamicOrKinematic;
			settings.mAllowedDOFs = (JPH::EAllowedDOFs)allowedDOF;
			settings.mIsSensor = isSensor;

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
	}

	this->shape = shape;
	this->allowedDOF = allowedDOF;
}

//**********************************************************************************************************************
bool RigidbodyComponent::canBeKinematicOrDynamic() const
{
	if (!shape)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->CanBeKinematicOrDynamic();
}

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
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
	bodyInterface->ActivateBody(body->GetID());
}
void RigidbodyComponent::deactivate()
{
	if (!shape || !inSimulation)
		return;
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
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

//**********************************************************************************************************************
float3 RigidbodyComponent::getPosition() const
{
	if (!shape)
		return float3(0.0f);
	auto body = (const JPH::Body*)instance;
	return toFloat3(body->GetPosition());
}
void RigidbodyComponent::setPosition(const float3& position, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
	bodyInterface->SetPosition(body->GetID(), toVec3(position),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = lastPosition;
}

quat RigidbodyComponent::getRotation() const
{
	if (!shape)
		return quat::identity;
	auto body = (const JPH::Body*)instance;
	return toQuat(body->GetRotation());
}
void RigidbodyComponent::setRotation(const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
	bodyInterface->SetRotation(body->GetID(), toQuat(rotation),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastRotation = rotation;
}

void RigidbodyComponent::getPosAndRot(float3& position, quat& rotation) const
{
	if (!shape)
	{
		position = float3(0.0f);
		rotation = quat::identity;
		return;
	}

	auto body = (const JPH::Body*)instance;
	position = toFloat3(body->GetPosition());
	rotation = toQuat(body->GetRotation());
}
void RigidbodyComponent::setPosAndRot(const float3& position, const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
	bodyInterface->SetPositionAndRotation(body->GetID(), toVec3(position), toQuat(rotation),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = position;
	lastRotation = rotation;
}
bool RigidbodyComponent::isPosAndRotChanged(const float3& position, const quat& rotation) const
{
	GARDEN_ASSERT(shape);
	auto body = (const JPH::Body*)instance;
	return !body->GetPosition().IsClose(toVec3(position)) || !body->GetRotation().IsClose(toQuat(rotation));
}

//**********************************************************************************************************************
float3 RigidbodyComponent::getLinearVelocity() const
{
	if (!shape)
		return float3(0.0f);
	auto body = (const JPH::Body*)instance;
	return toFloat3(body->GetLinearVelocity());
}
void RigidbodyComponent::setLinearVelocity(const float3& velocity)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(motionType != MotionType::Static);
	auto body = (JPH::Body*)instance;
	body->SetLinearVelocity(toVec3(velocity));
}

float3 RigidbodyComponent::getAngularVelocity() const
{
	if (!shape)
		return float3(0.0f);
	auto body = (const JPH::Body*)instance;
	return toFloat3(body->GetAngularVelocity());
}
void RigidbodyComponent::setAngularVelocity(const float3& velocity)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(motionType != MotionType::Static);
	auto body = (JPH::Body*)instance;
	body->SetAngularVelocity(toVec3(velocity));
}

float3 RigidbodyComponent::getPointVelocity(const float3& point) const
{
	if (!shape)
		return float3(0.0f);
	auto body = (const JPH::Body*)instance;
	return toFloat3(body->GetPointVelocity(toVec3(point)));
}
float3 RigidbodyComponent::getPointVelocityCOM(const float3& point) const
{
	if (!shape)
		return float3(0.0f);
	auto body = (JPH::Body*)instance;
	return toFloat3(body->GetPointVelocityCOM(toVec3(point)));
}

void RigidbodyComponent::moveKinematic(const float3& position, const quat& rotation, float deltaTime)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(motionType != MotionType::Static);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::get()->bodyInterface;
	bodyInterface->MoveKinematic(body->GetID(), toVec3(position), toQuat(rotation), deltaTime);
}

void RigidbodyComponent::setWorldTransform(bool activate)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(Manager::get()->has<TransformComponent>(entity));
	auto transformView = TransformSystem::get()->get(entity);
	auto model = transformView->calcModel();
	setPosAndRot(getTranslation(model), extractQuat(extractRotation(model)), activate);
}

//**********************************************************************************************************************
PhysicsSystem* PhysicsSystem::instance = nullptr;

PhysicsSystem::PhysicsSystem(const Properties& properties)
{
	this->hashState = Hash128::createState();

	auto manager = Manager::get();
	manager->registerEventBefore("Simulate", "Update");

	SUBSCRIBE_TO_EVENT("PreInit", PhysicsSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostInit", PhysicsSystem::postInit);
	SUBSCRIBE_TO_EVENT("Simulate", PhysicsSystem::simulate);
	
	// This needs to be done before any other Jolt function is called.
	JPH::RegisterDefaultAllocator(); 

	// Install trace and assert callbacks
	JPH::Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

	// Create a factory, this class is responsible for creating instances of classes 
	// based on their name or hash and is mainly used for deserialization of saved data.
	JPH::Factory::sInstance = new JPH::Factory();

	// TODO: register custom shapes, and our own default material

	// Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
	JPH::RegisterTypes();

	// We need a temp allocator for temporary allocations during the physics update.
	auto tempAllocator = new JPH::TempAllocatorImpl(properties.tempBufferSize);
	this->tempAllocator = tempAllocator;

	// Create mapping table from object layer to broadphase layer
	auto bpLayerInterface = new GardenBroadPhaseLayerInterface();
	this->bpLayerInterface = bpLayerInterface;

	// Create class that filters object vs broadphase layers
	auto objVsBpLayerFilter = new GardenObjectVsBroadPhaseLayerFilter();
	this->objVsBpLayerFilter = objVsBpLayerFilter;

	// Create class that filters object vs object layers
	auto objVsObjLayerFilter = new GardenObjectLayerPairFilter();
	this->objVsObjLayerFilter = objVsObjLayerFilter;

	auto physicsInstance = new JPH::PhysicsSystem();
	this->physicsInstance = physicsInstance;

	physicsInstance->Init(properties.maxRigidbodies, properties.bodyMutexCount, properties.maxBodyPairs,
		properties.maxContactConstraints, *bpLayerInterface, *objVsBpLayerFilter, *objVsObjLayerFilter);

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

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
PhysicsSystem::~PhysicsSystem()
{
	components.clear();
	shapes.clear();

	delete (GardenJobSystem*)jobSystem;
	delete (JPH::PhysicsSystem*)physicsInstance;
	delete (GardenContactListener*)contactListener;
	delete (GardenBodyActivationListener*)bodyListener;
	delete (GardenObjectLayerPairFilter*)objVsObjLayerFilter;
	delete (GardenObjectVsBroadPhaseLayerFilter*)objVsBpLayerFilter;
	delete (GardenBroadPhaseLayerInterface*)bpLayerInterface;
	delete (JPH::TempAllocatorImpl*)tempAllocator;
	JPH::UnregisterTypes();

	// Destroy the factory
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;

	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", PhysicsSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostInit", PhysicsSystem::postInit);
		UNSUBSCRIBE_FROM_EVENT("Simulate", PhysicsSystem::simulate);

		auto manager = Manager::get();
		manager->unregisterEvent("Simulate");
	}

	Hash128::destroyState(hashState);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}	

//**********************************************************************************************************************
void PhysicsSystem::preInit()
{
	auto threadSystem = Manager::get()->get<ThreadSystem>();
	auto threadPool = &threadSystem->getForegroundPool();

	// We need a job system that will execute physics jobs on multiple threads.
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
	if (components.getCount() > 0 && Manager::get()->has<TransformSystem>())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems(ThreadPool::Task([componentData](const ThreadPool::Task& task)
		{
			auto physicsInstance = (JPH::PhysicsSystem*)PhysicsSystem::get()->physicsInstance;
			auto& bodyInterface = physicsInstance->GetBodyInterface();
			auto transformSystem = TransformSystem::get();
			auto characterSystem = Manager::get()->tryGet<CharacterSystem>();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->entity)
					continue;

				auto transformView = transformSystem->tryGet(rigidbodyView->entity);
				if (!transformView)
					continue;

				auto isActive = transformView->isActiveWithAncestors();
				if (isActive)
				{
					if (!rigidbodyView->inSimulation)
					{
						if (rigidbodyView->instance)
						{
							auto body = (JPH::Body*)rigidbodyView->instance;
							bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
						}
						rigidbodyView->inSimulation = true;
					}
				}
				else
				{
					if (rigidbodyView->inSimulation)
					{
						if (rigidbodyView->instance)
						{
							auto body = (JPH::Body*)rigidbodyView->instance;
							bodyInterface.RemoveBody(body->GetID());
						}
						rigidbodyView->inSimulation = false;
					}
				}

				if (!rigidbodyView->instance || rigidbodyView->motionType == MotionType::Static ||
					(characterSystem && characterSystem->has(rigidbodyView->entity)))
				{
					continue;
				}

				float3 position; quat rotation;
				rigidbodyView->getPosAndRot(position, rotation);
				transformView->position = rigidbodyView->lastPosition = position;
				transformView->rotation = rigidbodyView->lastRotation = rotation;
			}
		}),
		components.getOccupancy());
		threadPool->wait();
	}
}

//**********************************************************************************************************************
void PhysicsSystem::processSimulate()
{
	auto& lockInterface = *((const JPH::BodyLockInterface*)this->lockInterface);

	for (psize i = 0; i < bodyEvents.size(); i++)
	{
		auto bodyEvent = bodyEvents[i];
		ID<Entity> entity1 = {}, entity2 = {};

		if (bodyEvent.eventType == BodyEvent::ContactRemoved)
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
			auto rigidbodyView = get(entity1);
			for (const auto& listener : rigidbodyView->listeners)
			{
				if (listener.eventType == bodyEvent.eventType)
					listener.callback(entity1, entity2);
			}
		}
		if (entity2)
		{
			auto rigidbodyView = get(entity2);
			for (const auto& listener : rigidbodyView->listeners)
			{
				if (listener.eventType == bodyEvent.eventType)
					listener.callback(entity2, entity1);
			}
		}
	}
	bodyEvents.clear();
}

//**********************************************************************************************************************
void PhysicsSystem::interpolateResult(float t)
{
	if (components.getCount() > 0 && Manager::get()->has<TransformSystem>())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems(ThreadPool::Task([componentData, t](const ThreadPool::Task& task)
		{
			auto characterSystem = Manager::get()->tryGet<CharacterSystem>();
			auto transformSystem = TransformSystem::get();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->entity || rigidbodyView->motionType == MotionType::Static ||
					(characterSystem && characterSystem->has(rigidbodyView->entity)))
				{
					continue;
				}

				auto transformView = transformSystem->tryGet(rigidbodyView->entity);
				if (!transformView)
					continue;

				float3 position; quat rotation;
				rigidbodyView->getPosAndRot(position, rotation);
				transformView->position = lerp(rigidbodyView->lastPosition, position, t);
				transformView->rotation = slerp(rigidbodyView->lastRotation, rotation, t);
			}
		}),
		components.getOccupancy());
		threadPool->wait();
	}
}

void PhysicsSystem::simulate()
{
	auto simDeltaTime = 1.0f / (float)(simulationRate + 1);
	auto deltaTime = (float)InputSystem::get()->getDeltaTime();
	deltaTimeAccum += deltaTime;

	if (deltaTimeAccum >= simDeltaTime)
	{
		prepareSimulate();

		auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
		auto tempAllocator = (JPH::TempAllocator*)this->tempAllocator;
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto stepCount = simDeltaTime != 0.0f ? (uint32)(deltaTimeAccum / simDeltaTime) : 1;

		if (cascadeLagCount > simulationRate * cascadeLagThreshold)
		{
			// Trying to recover from cascade chain lag. (snowball effect)
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
			physicsInstance->Update(deltaTimeAccum, collisionSteps, tempAllocator, jobSystem);

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
ID<Component> PhysicsSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void PhysicsSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<RigidbodyComponent>(instance));
}
void PhysicsSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<RigidbodyComponent>(source);
	auto destinationView = View<RigidbodyComponent>(destination);
	destinationView->destroy();

	destinationView->listeners = sourceView->listeners;
	destinationView->motionType = sourceView->motionType;
	destinationView->inSimulation = sourceView->inSimulation;

	destinationView->setShape(sourceView->shape, sourceView->isActive(), 
		sourceView->canBeKinematicOrDynamic(), sourceView->isSensor(), sourceView->allowedDOF);
	float3 position; quat rotation;
	sourceView->getPosAndRot(position, rotation);
	destinationView->setPosAndRot(position, rotation);

	if (sourceView->motionType != MotionType::Static)
	{
		destinationView->setLinearVelocity(sourceView->getLinearVelocity());
		destinationView->setAngularVelocity(sourceView->getAngularVelocity());
	}

	destinationView->lastPosition = sourceView->lastPosition;
	destinationView->lastRotation = sourceView->lastRotation;
}
const string& PhysicsSystem::getComponentName() const
{
	static const string name = "Rigidbody";
	return name;
}
type_index PhysicsSystem::getComponentType() const
{
	return typeid(RigidbodyComponent);
}
View<Component> PhysicsSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<RigidbodyComponent>(instance)));
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
		if (halfExtent != float3(0.5f))
			serializer.write(isInner ? "innerHalfExtent" : "halfExtent", halfExtent);
		auto convexRadius = shapeView->getBoxConvexRadius();
		if (convexRadius != 0.05f)
			serializer.write(isInner ? "innerConvexRadius" : "convexRadius", convexRadius);
		serializer.write(isInner ? "innerShapeType" : "shapeType", string_view("Box"));
	}
}
static void serializeDecoratedShape(ISerializer& serializer, ID<Shape> shape, const LinearPool<Shape>& shapes)
{
	auto shapeView = shapes.get(shape);
	auto subType = shapeView->getSubType();
	if (subType == ShapeSubType::RotatedTranslated)
	{
		float3 position; quat rotation;
		shapeView->getPosAndRot(position, rotation);
		if (position != float3(0.0f))
			serializer.write("shapePosition", position);
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

void PhysicsSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<RigidbodyComponent>(component);
	
	auto motionType = componentView->motionType;
	if (motionType == MotionType::Kinematic)
		serializer.write("motionType", string_view("Kinematic"));
	else if (motionType == MotionType::Dynamic)
		serializer.write("motionType", string_view("Dynamic"));

	if (componentView->shape)
	{
		if (componentView->isActive())
			serializer.write("isActive", true);
		if (componentView->canBeKinematicOrDynamic())
			serializer.write("allowDynamicOrKinematic", true);
		if (componentView->isSensor())
			serializer.write("isSensor", true);

		float3 position; quat rotation;
		componentView->getPosAndRot(position, rotation);
		if (position != float3(0.0f))
			serializer.write("position", position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

		auto velocity = componentView->getLinearVelocity();
		if (velocity != float3(0.0f))
			serializer.write("linearVelocity", position);
		velocity = componentView->getAngularVelocity();
		if (velocity != float3(0.0f))
			serializer.write("angularVelocity", position);

		if (componentView->motionType != MotionType::Static && 
			componentView->allowedDOF != AllowedDOF::All)
		{
			auto allowedDOF = componentView->allowedDOF;
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

		serializeDecoratedShape(serializer, componentView->shape, shapes);
	}
}

//**********************************************************************************************************************
static ID<Shape> deserializeShape(IDeserializer& deserializer, const string& shapeType, bool isInner)
{
	if (shapeType == "Box")
	{
		float3 halfExtent(0.5f); float convexRadius = 0.05f;
		deserializer.read(isInner ? "innerHalfExtent" : "halfExtent", halfExtent);
		deserializer.read(isInner ? "innerConvexRadius" : "convexRadius", convexRadius);
		return PhysicsSystem::get()->createSharedBoxShape(halfExtent, convexRadius);
	}
	return {};
}
static ID<Shape> deserializeDecoratedShape(IDeserializer& deserializer, string& valueStringCache)
{
	if (valueStringCache == "RotatedTranslated")
	{
		if (!deserializer.read("innerShapeType", valueStringCache))
			return {};
		auto innerShape = deserializeShape(deserializer, valueStringCache, true);
		if (!innerShape)
			return {};
		auto position = float3(0.0f);
		deserializer.read("shapePosition", position);
		auto rotation = quat::identity;
		deserializer.read("shapeRotation", rotation);
		return PhysicsSystem::get()->createSharedRotTransShape(innerShape, position, rotation);
	}
	return deserializeShape(deserializer, valueStringCache, false);
}

void PhysicsSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<RigidbodyComponent>(component);
	
	if (deserializer.read("motionType", valueStringCache))
	{
		if (valueStringCache == "Kinematic")
			componentView->motionType = MotionType::Kinematic;
		else if (valueStringCache == "Dynamic")
			componentView->motionType = MotionType::Dynamic;
	}

	if (deserializer.read("shapeType", valueStringCache))
	{
		auto shape = deserializeDecoratedShape(deserializer, valueStringCache);
		if (shape)
		{
			auto isActive = false, allowDynamicOrKinematic = false, isSensor = false;
			deserializer.read("isActive", isActive);
			deserializer.read("allowDynamicOrKinematic", allowDynamicOrKinematic);
			deserializer.read("isSensor", isSensor);

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

			componentView->setShape(shape, isActive, allowDynamicOrKinematic, isSensor, allowedDOF);

			auto position = float3(0.0f);
			deserializer.read("position", position);
			auto rotation = quat::identity;
			deserializer.read("rotation", rotation);
			if (position != float3(0.0f) || rotation != quat::identity)
				componentView->setPosAndRot(position, rotation, isActive);

			auto velocity = float3(0.0f);
			deserializer.read("linearVelocity", velocity);
			if (velocity != float3(0.0f))
				componentView->setLinearVelocity(velocity);
			velocity = float3(0.0f);
			deserializer.read("angularVelocity", velocity);
			if (velocity != float3(0.0f))
				componentView->setAngularVelocity(velocity);
		}
	}
}

//**********************************************************************************************************************
float3 PhysicsSystem::getGravity() const noexcept
{
	auto physicsInstance = (const JPH::PhysicsSystem*)this->physicsInstance;
	return toFloat3(physicsInstance->GetGravity());
}
void PhysicsSystem::setGravity(const float3& gravity) const noexcept
{
	auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
	physicsInstance->SetGravity(toVec3(gravity));
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createBoxShape(const float3& halfExtent, float convexRadius)
{
	GARDEN_ASSERT(halfExtent >= convexRadius);
	GARDEN_ASSERT(convexRadius >= 0.0f);
	auto boxShape = new JPH::BoxShape(toVec3(halfExtent), convexRadius);
	boxShape->AddRef();
	auto instance = shapes.create(boxShape);
	boxShape->SetUserData((JPH::uint64)*instance);
	return instance;
}

ID<Shape> PhysicsSystem::createSharedBoxShape(const float3& halfExtent, float convexRadius)
{
	GARDEN_ASSERT(halfExtent >= convexRadius);
	GARDEN_ASSERT(convexRadius >= 0.0f);

	Hash128 hash;
	auto hashBytes = (uint8*)&hash;
	*((float3*)hashBytes) = halfExtent;
	*((float*)(hashBytes + sizeof(float3))) = convexRadius;

	auto searchResult = sharedBoxShapes.find(hash);
	if (searchResult != sharedBoxShapes.end())
		return searchResult->second;

	auto boxShape = createBoxShape(halfExtent, convexRadius);
	auto emplaceResult = sharedBoxShapes.emplace(hash, boxShape);
	GARDEN_ASSERT(emplaceResult.second); // Corrupted memory detected.
	return boxShape;
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createRotTransShape(ID<Shape> innerShape, const float3& position, const quat& rotation)
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
ID<Shape> PhysicsSystem::createSharedRotTransShape(ID<Shape> innerShape, const float3& position, const quat& rotation)
{
	GARDEN_ASSERT(innerShape);

	Hash128::resetState(hashState);
	Hash128::updateState(hashState, &innerShape, sizeof(ID<Shape>));
	Hash128::updateState(hashState, &position, sizeof(float3));
	Hash128::updateState(hashState, &rotation, sizeof(float3));
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedRotTransShapes.find(hash);
	if (searchResult != sharedRotTransShapes.end())
		return searchResult->second;

	auto rotaTransShape = createRotTransShape(innerShape, position, rotation);
	auto emplaceResult = sharedRotTransShapes.emplace(hash, rotaTransShape);
	GARDEN_ASSERT(emplaceResult.second); // Corrupted memory detected.
	return rotaTransShape;
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
	if (subType == ShapeSubType::Box)
	{
		for (auto i = sharedBoxShapes.begin(); i != sharedBoxShapes.end(); i++)
		{
			if (i->second != shape)
				continue;
			sharedBoxShapes.erase(i);
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

	if (shapeView->isLastRef())
		destroy(ID<Shape>(shape));
}

//**********************************************************************************************************************
void PhysicsSystem::optimizeBroadPhase()
{
	auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
	physicsInstance->OptimizeBroadPhase();
}

void PhysicsSystem::activateRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);
	GARDEN_ASSERT(Manager::get()->has<TransformSystem>());

	auto transformSystem = TransformSystem::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto rigidbodyView = tryGet(entity);
		if (rigidbodyView)
			rigidbodyView->activate();

		auto transformView = transformSystem->tryGet(entity);
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
	GARDEN_ASSERT(Manager::get()->has<TransformSystem>());

	auto transformSystem = TransformSystem::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto rigidbodyView = tryGet(entity);
		if (rigidbodyView && rigidbodyView->getShape())
			rigidbodyView->setWorldTransform(activate);

		auto transformView = transformSystem->tryGet(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}

//**********************************************************************************************************************
bool PhysicsSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(RigidbodyComponent)) != entityComponents.end();
}
View<RigidbodyComponent> PhysicsSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(RigidbodyComponent));
	return components.get(ID<RigidbodyComponent>(pair.second));
}
View<RigidbodyComponent> PhysicsSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(RigidbodyComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<RigidbodyComponent>(result->second.second));
}

//**********************************************************************************************************************
bool CharacterComponent::destroy()
{
	if (instance)
	{
		auto instance = (JPH::CharacterVirtual*)this->instance;
		auto charVsCharCollision = (JPH::CharacterVsCharacterCollisionSimple*)
			CharacterSystem::get()->charVsCharCollision;
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
		auto physicsSystem = PhysicsSystem::get();
		auto shapeView = physicsSystem->get(shape);

		if (instance)
		{
			auto instance = (JPH::CharacterVirtual*)this->instance;
			auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
			auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
			auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
			auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(Layers::MOVING);
			JPH::BodyID rigidbodyID = {};

			auto rigidbodyView = physicsSystem->tryGet(entity);
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
			auto transformView = Manager::get()->tryGet<TransformComponent>(entity);
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
				CharacterSystem::get()->charVsCharCollision;
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
					CharacterSystem::get()->charVsCharCollision;
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
	GARDEN_ASSERT(shape);
	auto instance = (JPH::CharacterVirtual*)this->instance;
	auto physicsSystem = PhysicsSystem::get();
	auto physicsInstance = (JPH::PhysicsSystem*)physicsSystem->physicsInstance;
	auto tempAllocator = (JPH::TempAllocator*)physicsSystem->tempAllocator;
	auto bpLayerFilter = physicsInstance->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
	auto objectLayerFilter = physicsInstance->GetDefaultLayerFilter(Layers::MOVING);
	JPH::BodyID rigidbodyID = {};

	auto rigidbodyView = physicsSystem->tryGet(entity);
	if (rigidbodyView && rigidbodyView->instance)
	{
		auto instance = (JPH::Body*)rigidbodyView->instance;
		rigidbodyID = instance->GetID();
	}

	JPH::IgnoreSingleBodyFilter bodyFilter(rigidbodyID);
	JPH::ShapeFilter shapeFilter; // TODO: add collision matrix

	instance->Update(deltaTime, toVec3(gravity), bpLayerFilter, 
		objectLayerFilter, bodyFilter, shapeFilter, *tempAllocator);

	auto manager = Manager::get();
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (transformView)
	{
		float3 position; quat rotation;
		getPosAndRot(position, rotation);

		if (transformView->getParent())
		{
			auto parentView = TransformSystem::get()->get(transformView->getParent());
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
	GARDEN_ASSERT(Manager::get()->has<TransformComponent>(entity));
	auto transformView = TransformSystem::get()->get(entity);
	auto model = transformView->calcModel();
	setPosAndRot(getTranslation(model), extractQuat(extractRotation(model)));
}

//**********************************************************************************************************************
CharacterSystem* CharacterSystem::instance = nullptr;

CharacterSystem::CharacterSystem()
{
	auto charVsCharCollision = new JPH::CharacterVsCharacterCollisionSimple();
	this->charVsCharCollision = charVsCharCollision;

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
CharacterSystem::~CharacterSystem()
{
	components.clear();
	delete (JPH::CharacterVsCharacterCollisionSimple*)charVsCharCollision;

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
ID<Component> CharacterSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void CharacterSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<CharacterComponent>(instance));
}
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
type_index CharacterSystem::getComponentType() const
{
	return typeid(CharacterComponent);
}
View<Component> CharacterSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CharacterComponent>(instance)));
}
void CharacterSystem::disposeComponents()
{
	components.dispose();
}

//**********************************************************************************************************************
void CharacterSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<CharacterComponent>(component);

	if (componentView->shape)
	{
		auto mass = componentView->getMass();
		if (mass != 70.0f)
			serializer.write("mass", mass);

		float3 position; quat rotation;
		componentView->getPosAndRot(position, rotation);
		if (position != float3(0.0f))
			serializer.write("position", position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

		auto velocity = componentView->getLinearVelocity();
		if (velocity != float3(0.0f))
			serializer.write("linearVelocity", position);

		serializeDecoratedShape(serializer, componentView->shape, PhysicsSystem::get()->shapes);
	}
}
void CharacterSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<CharacterComponent>(component);

	if (deserializer.read("shapeType", valueStringCache))
	{
		auto shape = deserializeDecoratedShape(deserializer, valueStringCache);
		if (shape)
		{
			auto mass = 70.0f;
			deserializer.read("mass", mass);
			componentView->setShape(shape, mass);

			auto position = float3(0.0f);
			deserializer.read("position", position);
			auto rotation = quat::identity;
			deserializer.read("rotation", rotation);
			if (position != float3(0.0f) || rotation != quat::identity)
				componentView->setPosAndRot(position, rotation);

			auto velocity = float3(0.0f);
			deserializer.read("linearVelocity", velocity);
			if (velocity != float3(0.0f))
				componentView->setLinearVelocity(velocity);
		}
	}
}

//**********************************************************************************************************************
void CharacterSystem::setWorldTransformRecursive(ID<Entity> entity)
{
	GARDEN_ASSERT(entity);
	GARDEN_ASSERT(Manager::get()->has<TransformSystem>());

	auto transformSystem = TransformSystem::get();
	entityStack.push_back(entity);

	while (!entityStack.empty())
	{
		auto entity = entityStack.back();
		entityStack.pop_back();

		auto characterView = tryGet(entity);
		if (characterView && characterView->getShape())
			characterView->setWorldTransform();

		auto transformView = transformSystem->tryGet(entity);
		if (!transformView)
			continue;

		auto childCount = transformView->getChildCount();
		auto childs = transformView->getChilds();

		for (uint32 i = 0; i < childCount; i++)
			entityStack.push_back(childs[i]);
	}
}

bool CharacterSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(CharacterComponent)) != entityComponents.end();
}
View<CharacterComponent> CharacterSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(CharacterComponent));
	return components.get(ID<CharacterComponent>(pair.second));
}
View<CharacterComponent> CharacterSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::get()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(CharacterComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<CharacterComponent>(result->second.second));
}
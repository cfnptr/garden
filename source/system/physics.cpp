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

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

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

	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace(buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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
		ID<Entity> entity; *entity = inBodyUserData;
		auto rigidbodyView = physicsSystem->get(entity);

		if (!rigidbodyView->getListeners().empty())
		{
			PhysicsSystem::Event bodyEvent;
			bodyEvent.eventType = eventType;
			bodyEvent.data1 = inBodyUserData;
			bodyEventLocker->lock();
			bodyEvents->push_back(bodyEvent);
			bodyEventLocker->unlock();
		}
	}

	void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
		addEvent((uint32)inBodyUserData, BodyEvent::Activated);
	}
	void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
		addEvent((uint32)inBodyUserData, BodyEvent::Deactivated);
	}
};

//**********************************************************************************************************************
bool Shape::destroy()
{
	if (!instance)
		return true;

	auto instance = (JPH::Shape*)this->instance;
	if (instance->GetRefCount() > 1)
		return false;

	instance->Release();
	instance = nullptr;
	return true;
}

ShapeType Shape::getType() const
{
	GARDEN_ASSERT(instance);
	static_assert((uint32)ShapeType::User4 == (uint32)JPH::EShapeType::User4, 
		"ShapeType does not map to the EShapeType");
	auto instance = (JPH::Shape*)this->instance;
	return (ShapeType)instance->GetType();
}
ShapeSubType Shape::getSubType() const
{
	GARDEN_ASSERT(instance);
	static_assert((uint32)ShapeSubType::UserConvex8 == (uint32)JPH::EShapeSubType::UserConvex8,
		"ShapeSubType does not map to the EShapeSubType");
	auto instance = (JPH::Shape*)this->instance;
	return (ShapeSubType)instance->GetSubType();
}

//**********************************************************************************************************************
float3 Shape::getBoxHalfExtent() const
{
	GARDEN_ASSERT(instance);
	auto instance = (JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (JPH::BoxShape*)this->instance;
	auto halfExtent = boxInstance->GetHalfExtent();
	return float3(halfExtent.GetX(), halfExtent.GetY(), halfExtent.GetZ());
}
float Shape::getBoxConvexRadius() const
{
	GARDEN_ASSERT(instance);
	auto instance = (JPH::Shape*)this->instance;
	GARDEN_ASSERT(instance->GetSubType() == JPH::EShapeSubType::Box);
	auto boxInstance = (JPH::BoxShape*)this->instance;
	return boxInstance->GetConvexRadius();
}

uint64 Shape::getRefCount() const
{
	GARDEN_ASSERT(instance);
	auto instance = (JPH::Shape*)this->instance;
	return instance->GetRefCount();
}
bool Shape::isLastRef() const
{
	GARDEN_ASSERT(instance);
	auto instance = (JPH::Shape*)this->instance;
	return instance->GetRefCount() == 1;
}

//**********************************************************************************************************************
bool RigidbodyComponent::destroy()
{
	if (instance)
	{
		auto body = (JPH::Body*)instance;
		auto physicsSystem = PhysicsSystem::getInstance();
		auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;
		if (inSimulation)
			bodyInterface->RemoveBody(body->GetID());
		bodyInterface->DestroyBody(body->GetID());
		physicsSystem->destroyShared(shape);

		instance = nullptr;
		shape = {};
	}

	return true;
}

//**********************************************************************************************************************
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
		auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
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
	auto physicsSystem = PhysicsSystem::getInstance();
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
			auto transformView = Manager::getInstance()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				position = lastPosition = transformView->position;
				rotation = lastRotation = transformView->rotation;
			}

			JPH::BodyCreationSettings settings(shapeInstance,
				JPH::RVec3(position.x, position.y, position.z),
				JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
				(JPH::EMotionType)motionType, motionType == MotionType::Static ?
				Layers::NON_MOVING : Layers::MOVING);
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

			this->allowedDOF = motionType == MotionType::Static ?
				AllowedDOF::None : allowedDOF;
			instance = body;
		}
	}
	else
	{
		if (instance)
		{
			if (inSimulation)
				bodyInterface->RemoveBody(body->GetID());
			bodyInterface->DestroyBody(body->GetID());

			allowedDOF = {};
			instance = nullptr;
		}
	}

	this->shape = shape;
}

//**********************************************************************************************************************
bool RigidbodyComponent::canBeKinematicOrDynamic() const noexcept
{
	if (!instance)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->CanBeKinematicOrDynamic();
}

bool RigidbodyComponent::isActive() const
{
	if (!instance)
		return false;
	auto body = (const JPH::Body*)instance;
	return body->IsActive();
}
void RigidbodyComponent::activate()
{
	if (!instance || !inSimulation)
		return;
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->ActivateBody(body->GetID());
}
void RigidbodyComponent::deactivate()
{
	if (!instance || !inSimulation)
		return;
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->DeactivateBody(body->GetID());
}

//**********************************************************************************************************************
float3 RigidbodyComponent::getPosition() const
{
	if (!instance)
		return float3(0.0f);
	auto body = (const JPH::Body*)instance;
	auto position = body->GetPosition();
	return float3(position.GetX(), position.GetY(), position.GetZ());
}
void RigidbodyComponent::setPosition(const float3& position, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetPosition(body->GetID(), JPH::RVec3(position.x, position.y, position.z),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = lastPosition;
}

quat RigidbodyComponent::getRotation() const
{
	if (!instance)
		return quat::identity;
	auto body = (const JPH::Body*)instance;
	auto rotation = body->GetRotation();
	return quat(rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW());
}
void RigidbodyComponent::setRotation(const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetRotation(body->GetID(), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastRotation = rotation;
}

void RigidbodyComponent::getPosAndRot(float3& position, quat& rotation) const
{
	if (!instance)
	{
		position = float3(0.0f);
		rotation = quat::identity;
		return;
	}

	auto body = (const JPH::Body*)instance;
	auto pos = body->GetPosition();
	auto rot = body->GetRotation();
	position = float3(pos.GetX(), pos.GetY(), pos.GetZ());
	rotation = quat(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
}
void RigidbodyComponent::setPosAndRot(const float3& position, const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetPositionAndRotation(body->GetID(),
		JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	lastPosition = position;
	lastRotation = rotation;
}

void RigidbodyComponent::moveKinematic(const float3& position, const quat& rotation, float deltaTime)
{
	GARDEN_ASSERT(shape);
	auto body = (JPH::Body*)instance;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->MoveKinematic(body->GetID(), JPH::RVec3(position.x, position.y, position.z),
		JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), deltaTime);
}

//**********************************************************************************************************************
bool RigidbodyComponent::isSensor() const
{
	if (!instance)
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
PhysicsSystem* PhysicsSystem::instance = nullptr;

PhysicsSystem::PhysicsSystem(const Properties& properties)
{
	auto manager = Manager::getInstance();
	manager->registerEventBefore("Simulate", "Update");
	SUBSCRIBE_TO_EVENT("PreInit", PhysicsSystem::preInit);
	SUBSCRIBE_TO_EVENT("PostInit", PhysicsSystem::postInit);
	SUBSCRIBE_TO_EVENT("PostDeinit", PhysicsSystem::postDeinit);
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

	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("PreInit", PhysicsSystem::preInit);
		UNSUBSCRIBE_FROM_EVENT("PostInit", PhysicsSystem::postInit);
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", PhysicsSystem::postDeinit);
		UNSUBSCRIBE_FROM_EVENT("Simulate", PhysicsSystem::simulate);
		manager->unregisterEvent("Simulate");
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}	

//**********************************************************************************************************************
void PhysicsSystem::preInit()
{
	auto threadSystem = Manager::getInstance()->get<ThreadSystem>();
	auto threadPool = &threadSystem->getForegroundPool();

	// We need a job system that will execute physics jobs on multiple threads.
	auto jobSystem = new GardenJobSystem(threadPool);
	this->jobSystem = jobSystem;
}
void PhysicsSystem::postInit()
{
	optimizeBroadPhase();
}
void PhysicsSystem::postDeinit()
{
	components.clear();
	shapes.clear();
}

//**********************************************************************************************************************
void PhysicsSystem::prepareSimulate()
{
	if (components.getCount() > 0 && Manager::getInstance()->has<TransformSystem>())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems(ThreadPool::Task([componentData](const ThreadPool::Task& task)
		{
			auto physicsSystem = (JPH::PhysicsSystem*)PhysicsSystem::getInstance()->physicsInstance;
			auto& bodyInterface = physicsSystem->GetBodyInterface();
			auto transformSystem = TransformSystem::getInstance();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->getEntity())
					continue;

				auto transformView = transformSystem->tryGet(rigidbodyView->getEntity());
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

				if (!rigidbodyView->instance || rigidbodyView->motionType == MotionType::Static)
					continue;

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
	if (components.getCount() > 0 && Manager::getInstance()->has<TransformSystem>())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems(ThreadPool::Task([componentData, t](const ThreadPool::Task& task)
		{
			auto transformSystem = TransformSystem::getInstance();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyView = &componentData[i];
				if (!rigidbodyView->instance || rigidbodyView->motionType == MotionType::Static)
					continue;
				auto transformView = transformSystem->tryGet(rigidbodyView->getEntity());
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
	auto deltaTime = (float)InputSystem::getInstance()->getDeltaTime();
	deltaTimeAccum += deltaTime;

	if (deltaTimeAccum >= simDeltaTime)
	{
		prepareSimulate();
		auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
		auto tempAllocator = (JPH::TempAllocator*)this->tempAllocator;
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		physicsInstance->Update(deltaTimeAccum, collisionSteps, tempAllocator, jobSystem);
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

	destinationView->motionType = sourceView->motionType;
	destinationView->inSimulation = sourceView->inSimulation;
	destinationView->setShape(sourceView->shape, sourceView->isActive(), 
		sourceView->canBeKinematicOrDynamic(), sourceView->isSensor(), sourceView->allowedDOF);
	float3 position; quat rotation;
	sourceView->getPosAndRot(position, rotation);
	destinationView->setPosAndRot(position, rotation);
	// TODO: copy other properties
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
void PhysicsSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<RigidbodyComponent>(component);
	
	auto motionType = componentView->motionType;
	if (motionType == MotionType::Kinematic)
		serializer.write("motionType", string_view("Kinematic"));
	else if (motionType == MotionType::Dynamic)
		serializer.write("motionType", string_view("Dynamic"));

	auto shape = componentView->shape;
	if (shape)
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

		auto allowedDOF = componentView->allowedDOF;
		if (allowedDOF != AllowedDOF::All)
		{
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

		auto shapeView = shapes.get(shape);
		auto subType = shapeView->getSubType();
		if (subType == ShapeSubType::Box)
		{
			auto halfExtent = shapeView->getBoxHalfExtent();
			if (halfExtent != float3(0.5f))
				serializer.write("halfExtent", halfExtent);
			auto convexRadius = shapeView->getBoxConvexRadius();
			if (convexRadius != 0.05f)
				serializer.write("convexRadius", convexRadius);
			serializer.write("shapeType", string_view("Box"));
		}
	}
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

		if (valueStringCache == "Box")
		{
			float3 halfExtent(0.5f); float convexRadius = 0.05f;
			deserializer.read("halfExtent", halfExtent);
			deserializer.read("convexRadius", convexRadius);
			auto shape = createSharedBoxShape(halfExtent, convexRadius);
			componentView->setShape(shape, isActive, allowDynamicOrKinematic, isSensor, allowedDOF);
		}

		if (componentView->getShape())
		{
			auto position = float3(0.0f); auto rotation = quat::identity;
			deserializer.read("position", position);
			deserializer.read("rotation", rotation);
			if (position != float3(0.0f) || rotation != quat::identity)
				componentView->setPosAndRot(position, rotation, isActive);
		}
	}
}

//**********************************************************************************************************************
ID<Shape> PhysicsSystem::createBoxShape(const float3& halfExtent, float convexRadius)
{
	GARDEN_ASSERT(halfExtent >= convexRadius);
	GARDEN_ASSERT(convexRadius >= 0.0f);
	auto bodyInterface = (JPH::BodyInterface*)this->bodyInterface;
	auto boxShape = new JPH::BoxShape(JPH::Vec3(halfExtent.x, halfExtent.y, halfExtent.z), convexRadius);
	boxShape->AddRef();
	return shapes.create(boxShape);
}

//**********************************************************************************************************************
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

	if (shapeView->isLastRef())
		destroy(ID<Shape>(shape));
}

void PhysicsSystem::optimizeBroadPhase()
{
	auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
	physicsInstance->OptimizeBroadPhase();
}

//**********************************************************************************************************************
bool PhysicsSystem::has(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	return entityComponents.find(typeid(TransformComponent)) != entityComponents.end();
}
View<RigidbodyComponent> PhysicsSystem::get(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& pair = entityView->getComponents().at(typeid(RigidbodyComponent));
	return components.get(ID<RigidbodyComponent>(pair.second));
}
View<RigidbodyComponent> PhysicsSystem::tryGet(ID<Entity> entity) const
{
	GARDEN_ASSERT(entity);
	const auto entityView = Manager::getInstance()->getEntities().get(entity);
	const auto& entityComponents = entityView->getComponents();
	auto result = entityComponents.find(typeid(RigidbodyComponent));
	if (result == entityComponents.end())
		return {};
	return components.get(ID<RigidbodyComponent>(result->second.second));
}
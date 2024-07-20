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
public:
	JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) final
	{
		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) final
	{
	}
	void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, 
		const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) final
	{
	}
	void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) final
	{
	}
};

class GardenBodyActivationListener : public JPH::BodyActivationListener
{
public:
	void OnBodyActivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
	}
	void OnBodyDeactivated(const JPH::BodyID& inBodyID, JPH::uint64 inBodyUserData) final
	{
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
		auto physicsSystem = PhysicsSystem::getInstance();
		auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;
		if (inSimulation)
			bodyInterface->RemoveBody(JPH::BodyID(instance));
		bodyInterface->DestroyBody(JPH::BodyID(instance));
		physicsSystem->destroyShared(shape);

		instance = 0;
		shape = {};
	}

	return true;
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

		auto physicsSystem = PhysicsSystem::getInstance();
		auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;
		bodyInterface->SetMotionType(JPH::BodyID(instance), (JPH::EMotionType)motionType,
			activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		bodyInterface->SetObjectLayer(JPH::BodyID(instance),
			motionType == MotionType::Static ? Layers::NON_MOVING : Layers::MOVING);
	}

	this->motionType = motionType;
}

//**********************************************************************************************************************
void RigidbodyComponent::setShape(ID<Shape> shape, bool activate, bool allowDynamicOrKinematic)
{
	if (this->shape == shape)
		return;

	auto physicsSystem = PhysicsSystem::getInstance();
	auto bodyInterface = (JPH::BodyInterface*)physicsSystem->bodyInterface;

	if (shape)
	{
		auto shapeView = physicsSystem->get(shape);
		auto shapeInstance = (JPH::Shape*)shapeView->instance;

		if (instance)
		{
			bodyInterface->SetShape(JPH::BodyID(instance), shapeInstance, true,
				activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		}
		else
		{
			auto position = float3(0.0f); auto rotation = quat::identity;
			auto transformView = Manager::getInstance()->tryGet<TransformComponent>(entity);
			if (transformView)
			{
				position = transformView->position;
				rotation = transformView->rotation;
			}

			JPH::BodyCreationSettings settings(shapeInstance,
				JPH::RVec3(position.x, position.y, position.z),
				JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
				(JPH::EMotionType)motionType, motionType == MotionType::Static ?
				Layers::NON_MOVING : Layers::MOVING);
			settings.mAllowDynamicOrKinematic = allowDynamicOrKinematic;
			auto body = bodyInterface->CreateBody(settings);
			if (inSimulation)
			{
				bodyInterface->AddBody(body->GetID(), activate ?
					JPH::EActivation::Activate : JPH::EActivation::DontActivate);
			}
			instance = body->GetID().GetIndexAndSequenceNumber();
		}
	}
	else
	{
		if (instance)
		{
			if (inSimulation)
				bodyInterface->RemoveBody(JPH::BodyID(instance));
			bodyInterface->DestroyBody(JPH::BodyID(instance));
			instance = 0;
		}
	}

	this->shape = shape;
}

//**********************************************************************************************************************
bool RigidbodyComponent::canBeKinematicOrDynamic() const
{
	if (!instance)
		return false;
	auto lockInterface = (const JPH::BodyLockInterface*)PhysicsSystem::getInstance()->lockInterface;
	JPH::BodyLockRead lock(*lockInterface, JPH::BodyID(instance));
	if (lock.Succeeded())
	{
		auto& body = lock.GetBody();
		return body.CanBeKinematicOrDynamic();
	}
	return false;
}

bool RigidbodyComponent::isActive() const
{
	if (!instance)
		return false;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	return bodyInterface->IsActive(JPH::BodyID(instance));
}
void RigidbodyComponent::activate()
{
	if (!instance || !inSimulation)
		return;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->ActivateBody(JPH::BodyID(instance));
}
void RigidbodyComponent::deactivate()
{
	if (!instance || !inSimulation)
		return;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->DeactivateBody(JPH::BodyID(instance));
}

//**********************************************************************************************************************
float3 RigidbodyComponent::getPosition() const
{
	if (!instance)
		return float3(0.0f);
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	auto position = bodyInterface->GetPosition(JPH::BodyID(instance));
	return float3(position.GetX(), position.GetY(), position.GetZ());
}
void RigidbodyComponent::setPosition(const float3& position, bool activate)
{
	GARDEN_ASSERT(shape);
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetPosition(JPH::BodyID(instance), JPH::RVec3(position.x, position.y, position.z),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

quat RigidbodyComponent::getRotation() const
{
	if (!instance)
		return quat::identity;
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	auto rotation = bodyInterface->GetRotation(JPH::BodyID(instance));
	return quat(rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW());
}
void RigidbodyComponent::setRotation(const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetRotation(JPH::BodyID(instance), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void RigidbodyComponent::getPosAndRot(float3& position, quat& rotation) const
{
	if (!instance)
	{
		position = float3(0.0f);
		rotation = quat::identity;
		return;
	}

	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	JPH::RVec3 pos; JPH::Quat rot;
	bodyInterface->GetPositionAndRotation(JPH::BodyID(instance), pos, rot);
	position = float3(pos.GetX(), pos.GetY(), pos.GetZ());
	rotation = quat(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
}
void RigidbodyComponent::setPosAndRot(const float3& position, const quat& rotation, bool activate)
{
	GARDEN_ASSERT(shape);
	auto bodyInterface = (JPH::BodyInterface*)PhysicsSystem::getInstance()->bodyInterface;
	bodyInterface->SetPositionAndRotation(JPH::BodyID(instance), 
		JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

//**********************************************************************************************************************
PhysicsSystem* PhysicsSystem::instance = nullptr;

PhysicsSystem::PhysicsSystem(const Properties& properties)
{
	auto manager = Manager::getInstance();
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
	bodyInterface = &physicsInstance->GetBodyInterfaceNoLock(); ///< Version that does not lock the bodies, use with great care!
	lockInterface = &physicsInstance->GetBodyLockInterfaceNoLock();

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
PhysicsSystem::~PhysicsSystem()
{
	components.clear();
	shapes.clear();

	delete (GardenJobSystem*)jobSystem;
	delete (JPH::PhysicsSystem*)physicsInstance;
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

//**********************************************************************************************************************
void PhysicsSystem::updateInSimulation()
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
				auto rigidbodyComponent = &componentData[i];
				auto transformComponent = transformSystem->tryGet(rigidbodyComponent->getEntity());
				if (!transformComponent)
					continue;

				auto isActive = transformComponent->isActiveWithAncestors();
				if (isActive)
				{
					if (!rigidbodyComponent->inSimulation)
					{
						if (rigidbodyComponent->instance)
						{
							bodyInterface.AddBody(JPH::BodyID(
								rigidbodyComponent->instance), JPH::EActivation::Activate);
						}
						rigidbodyComponent->inSimulation = true;
					}
				}
				else
				{
					if (rigidbodyComponent->inSimulation)
					{
						if (rigidbodyComponent->instance)
							bodyInterface.RemoveBody(JPH::BodyID(rigidbodyComponent->instance));
						rigidbodyComponent->inSimulation = false;
					}
				}
			}
		}),
		components.getOccupancy());
		threadPool->wait();
	}
}

//**********************************************************************************************************************
void PhysicsSystem::interpolateResult()
{
	if (components.getCount() > 0 && Manager::getInstance()->has<TransformSystem>())
	{
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		auto threadPool = jobSystem->getThreadPool();
		auto componentData = components.getData();

		threadPool->addItems(ThreadPool::Task([componentData](const ThreadPool::Task& task)
		{
			auto transformSystem = TransformSystem::getInstance();
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto rigidbodyComponent = &componentData[i];
				if (!rigidbodyComponent->instance ||
					rigidbodyComponent->motionType == MotionType::Static)
				{
					continue;
				}

				auto transformComponent = transformSystem->tryGet(rigidbodyComponent->getEntity());
				if (!transformComponent)
					continue;

				float3 position; quat rotation;
				rigidbodyComponent->getPosAndRot(position, rotation);
				// TODO: interpolate based on delta time
				transformComponent->position = position;
				transformComponent->rotation = rotation;
			}
		}),
		components.getOccupancy());
		threadPool->wait();
	}
}

void PhysicsSystem::simulate()
{
	auto deltaTime = (float)InputSystem::getInstance()->getDeltaTime();
	deltaTimeAccum += deltaTime;

	if (deltaTimeAccum >= 1.0f / (float)(simulationRate + 1))
	{
		updateInSimulation();

		auto physicsInstance = (JPH::PhysicsSystem*)this->physicsInstance;
		auto tempAllocator = (JPH::TempAllocator*)this->tempAllocator;
		auto jobSystem = (GardenJobSystem*)this->jobSystem;
		physicsInstance->Update(deltaTimeAccum, collisionSteps, tempAllocator, jobSystem);
		deltaTimeAccum = 0.0f;
	}

	interpolateResult();
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

		float3 position; quat rotation;
		componentView->getPosAndRot(position, rotation);
		if (position != float3(0.0f))
			serializer.write("position", position);
		if (rotation != quat::identity)
			serializer.write("rotation", rotation);

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
	
	string stringValue;
	if (deserializer.read("motionType", stringValue))
	{
		if (stringValue == "Kinematic")
			componentView->motionType = MotionType::Kinematic;
		else if (stringValue == "Dynamic")
			componentView->motionType = MotionType::Dynamic;
	}

	if (deserializer.read("shapeType", stringValue))
	{
		auto isActive = false, allowDynamicOrKinematic = false;
		deserializer.read("isActive", isActive);
		deserializer.read("allowDynamicOrKinematic", allowDynamicOrKinematic);

		if (stringValue == "Box")
		{
			float3 halfExtent(0.5f); float convexRadius = 0.05f;
			deserializer.read("halfExtent", halfExtent);
			deserializer.read("convexRadius", convexRadius);
			auto shape = createSharedBoxShape(halfExtent, convexRadius);
			componentView->setShape(shape, isActive, allowDynamicOrKinematic);
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
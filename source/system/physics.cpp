//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/physics.hpp"
#include "garden/system/editor/physics.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/log.hpp"

#include "PxConfig.h"
#include "PxPhysicsAPI.h"

#define SCRATCH_BUFFER_SIZE 65536

using namespace physx;
using namespace garden;

namespace
{

class GardenPxErrorCallback final : public PxErrorCallback
{
	LogSystem* logSystem = nullptr;
public:
	GardenPxErrorCallback(LogSystem* _logSystem = nullptr) :
		logSystem(_logSystem) { }

	void reportError(PxErrorCode::Enum code, const char* message,
		const char* file, int line) final
	{
		if (!logSystem) return;
		LogSystem::Severity severity;
		switch (code)
		{
		case PxErrorCode::eDEBUG_INFO:
			severity = LogSystem::Severity::Debug; break;
		case PxErrorCode::eDEBUG_WARNING:
		case PxErrorCode::ePERF_WARNING:
			severity = LogSystem::Severity::Warn; break;
		case PxErrorCode::eABORT:
			severity = LogSystem::Severity::Fatal; break;
		default: severity = LogSystem::Severity::Error; break;
		}
		logSystem->log(severity, message);
		if (severity == LogSystem::Severity::Fatal) abort();
	}
};

class GardenPxCpuDispatcher final : public PxCpuDispatcher
{
	ThreadPool* threadPool = nullptr;
public:
	GardenPxCpuDispatcher(ThreadPool* _threadPool = nullptr) :
		threadPool(_threadPool) { }

	void submitTask(PxBaseTask& task) final
	{
		threadPool->addTask(ThreadPool::Task([](const ThreadPool::Task& task)
		{
			auto pxTask = (PxBaseTask*)task.getArgument();
			pxTask->run(); pxTask->release();
		}, &task));
	}

	uint32_t getWorkerCount() const final
	{
		return threadPool->getThreadCount();
	}
};

class GardenPxSimulation final : public PxSimulationEventCallback
{
private:
	PhysicsSystem* physicsSystem = nullptr;
public:
	GardenPxSimulation(PhysicsSystem* _physicsSystem = nullptr) :
		physicsSystem(_physicsSystem) { }

	void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) final { }
	void onWake(PxActor** actors, PxU32 count) final { }
	void onSleep(PxActor** actors, PxU32 count) final { }
	void onContact(const PxContactPairHeader& pairHeader,
		const PxContactPair* pairs, PxU32 nbPairs) final { }
	void onAdvance(const PxRigidBody*const* bodyBuffer,
		const PxTransform* poseBuffer, const PxU32 count) final { }

	void onTrigger(PxTriggerPair* pairs, PxU32 count) final
	{
		auto manager = physicsSystem->getManager();
		auto& subsystems = manager->getSubsystems<PhysicsSystem>();

		for (auto subsystem : subsystems)
		{
			auto system = dynamic_cast<IPhysicsSystem*>(subsystem.system);
			IPhysicsSystem::TriggerData data;

			for (PxU32 i = 0; i < count; i++)
			{
				auto& pair = pairs[i];

				if (pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
					PxTriggerPairFlag::eREMOVED_SHAPE_OTHER)) continue;
				
				*data.triggerEntity = (uint32)(psize)pair.triggerActor->userData;
				*data.otherEntity = (uint32)(psize)pair.otherActor->userData;
				*data.triggerShape = (uint32)(psize)pair.triggerShape->userData;
				*data.otherShape = (uint32)(psize)pair.otherShape->userData;
				data.isEntered = pair.status & PxPairFlag::eNOTIFY_TOUCH_FOUND;
				system->onTrigger(data);
			}
		}
	}
};

struct PhysicsData
{
	PxDefaultAllocator defaultAllocatorCallback;
	GardenPxErrorCallback gardenErrorCallback;
	GardenPxCpuDispatcher gardenCpuDispatcher;
	GardenPxSimulation gardenSimulation;
	PxCpuDispatcher* defaultCpuDispatcher = nullptr;
};

}

//--------------------------------------------------------------------------------------------------
bool Material::destroy()
{
	if (instance)
	{
		((PxMaterial*)instance)->release();
		instance = nullptr;
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
float Material::getStaticFriction() const
{
	return ((PxMaterial*)instance)->getStaticFriction();
}
void Material::setStaticFriction(float value)
{
	((PxMaterial*)instance)->setStaticFriction(value);
}

float Material::getDynamicFriction() const
{
	return ((PxMaterial*)instance)->getDynamicFriction();
}
void Material::setDynamicFriction(float value)
{
	((PxMaterial*)instance)->setDynamicFriction(value);
}

float Material::getRestitution() const
{
	return ((PxMaterial*)instance)->getRestitution();
}
void Material::setRestitution(float value)
{
	((PxMaterial*)instance)->setRestitution(value);
}

//--------------------------------------------------------------------------------------------------
bool Shape::destroy()
{
	if (instance)
	{
		((PxShape*)instance)->release();
		instance = nullptr;
	}

	return true;
}

void Shape::setMaterial(ID<Material> material)
{
	GARDEN_ASSERT(material);
	auto materialView = physicsSystem->getMaterials().get(material);
	auto pxMaterial = (PxMaterial*)materialView->getInstance();
	((PxShape*)instance)->setMaterials(&pxMaterial, 1);
	this->material = material;
}

void Shape::getPose(float3& position, quat& rotation) const
{
	auto pose = ((PxShape*)instance)->getLocalPose();
	position = float3(pose.p.x, pose.p.y, pose.p.z);
	rotation = quat(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
}
void Shape::setPose(const float3& position, const quat& rotation)
{
	((PxShape*)instance)->setLocalPose(PxTransform(position.x, position.y, position.z,
		PxQuat(rotation.x, rotation.y, rotation.z, rotation.w)));
}

bool Shape::isTrigger() const
{
	return ((PxShape*)instance)->getFlags() & PxShapeFlag::eTRIGGER_SHAPE;
}
void Shape::setTrigger(bool value)
{
	auto instance = (PxShape*)this->instance;
	instance->setFlag(PxShapeFlag::eSIMULATION_SHAPE, !value);
	instance->setFlag(PxShapeFlag::eTRIGGER_SHAPE, value);
}

//--------------------------------------------------------------------------------------------------
void Shape::setGeometry(Shape::Type type, void* geometry)
{
	GARDEN_ASSERT(geometry);
	((PxShape*)instance)->setGeometry(*((PxGeometry*)geometry));
	this->type = type;
}
void Shape::setBoxGeometry(const float3& halfSize)
{
	auto geometry = PxBoxGeometry(halfSize.x, halfSize.y, halfSize.z);
	setGeometry(Shape::Type::Cube, (PxGeometry*)&geometry);
}
void Shape::setSphereGeometry(float radius)
{
	auto geometry = PxSphereGeometry(radius);
	setGeometry(Shape::Type::Sphere, (PxGeometry*)&geometry);
}
void Shape::setPlaneGeometry()
{
	auto geometry = PxPlaneGeometry();
	setGeometry(Shape::Type::Plane, (PxGeometry*)&geometry);
}
void Shape::setCapsuleGeometry(float radius, float halfHeight)
{
	auto geometry = PxCapsuleGeometry(radius, halfHeight);
	setGeometry(Shape::Type::Capsule, (PxGeometry*)&geometry);
}
void Shape::setCustomGeometry(void* callbacks)
{
	auto geometry = PxCustomGeometry(*((PxCustomGeometry::Callbacks*)callbacks));
	setGeometry(Shape::Type::Custom, (PxGeometry*)&geometry);
}

//--------------------------------------------------------------------------------------------------
float Shape::getContactOffset() const
{
	return ((PxShape*)instance)->getContactOffset();
}
void Shape::setContactOffset(float value)
{
	GARDEN_ASSERT(value > ((PxShape*)instance)->getRestOffset());
	return ((PxShape*)instance)->setContactOffset(value);
}

float Shape::getRestOffset() const
{
	return ((PxShape*)instance)->getRestOffset();
}
void Shape::setRestOffset(float value)
{
	GARDEN_ASSERT(value < ((PxShape*)instance)->getContactOffset());
	return ((PxShape*)instance)->setRestOffset(value);
}

//--------------------------------------------------------------------------------------------------
bool RigidBodyComponent::destroy()
{
	if (instance)
	{
		((PxRigidActor*)instance)->release();
		instance = nullptr;
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
void RigidBodyComponent::setStatic(bool isStatic)
{
	if (instance && isStatic == this->staticBody) return;

	auto scene = (PxScene*)physicsSystem->getScene();
	auto physics = (PxPhysics*)physicsSystem->getInstance();
	vector<PxShape*> shapes; PxU32 shapeCount = 0;

	if (this->instance)
	{
		auto instance = (PxRigidActor*)this->instance;
		shapeCount = instance->getNbShapes();
		shapes.resize(shapeCount);
		instance->getShapes(shapes.data(), shapeCount);
		scene->removeActor(*instance);
		instance->release();
	}

	auto manager = physicsSystem->getManager();
	auto transformComponent = manager->get<TransformComponent>(entity);
	auto position = transformComponent->position;
	auto rotation = transformComponent->rotation;
	auto pxTransform = PxTransform(position.x, position.y, position.z,
		PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));

	PxRigidActor* rigidBody;
	if (isStatic) rigidBody = physics->createRigidStatic(pxTransform);
	else rigidBody = physics->createRigidDynamic(pxTransform);
	rigidBody->userData = (void*)(psize)*entity;

	if (shapeCount > 0)
	{
		for (PxU32 i = 0; i < shapeCount; i++)
			rigidBody->attachShape(*shapes[i]);
	}

	if (!isStatic)
	{
		auto dynamicBody = (PxRigidDynamic*)rigidBody;
		PxRigidBodyExt::updateMassAndInertia(*dynamicBody, 1.0f);
	}

	#if GARDEN_DEBUG
	rigidBody->setActorFlag(PxActorFlag::eVISUALIZATION, true);
	#endif

	this->instance = rigidBody;
	this->staticBody = isStatic;
	scene->addActor(*rigidBody);
}

//--------------------------------------------------------------------------------------------------
bool RigidBodyComponent::isSleeping() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->isSleeping();
}
void RigidBodyComponent::putToSleep()
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->putToSleep();
}
void RigidBodyComponent::wakeUp()
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->wakeUp();
}

//--------------------------------------------------------------------------------------------------
float RigidBodyComponent::getLinearDamping() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getLinearDamping();
}
void RigidBodyComponent::setLinearDamping(float value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setLinearDamping(value);
}

float RigidBodyComponent::getAngularDamping() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getAngularDamping();
}
void RigidBodyComponent::setAngularDamping(float value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setAngularDamping(value);
}

float3 RigidBodyComponent::getLinearVelocity() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	auto value = ((PxRigidDynamic*)instance)->getLinearVelocity();
	return float3(value.x, value.y, value.z);
}
void RigidBodyComponent::setLinearVelocity(const float3& value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setLinearVelocity(PxVec3(value.x, value.y, value.z));
}

float3 RigidBodyComponent::getAngularVelocity() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	auto value = ((PxRigidDynamic*)instance)->getAngularVelocity();
	return float3(value.x, value.y, value.z);
}
void RigidBodyComponent::setAngularVelocity(const float3& value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setAngularVelocity(PxVec3(value.x, value.y, value.z));
}

//--------------------------------------------------------------------------------------------------
float RigidBodyComponent::getMass() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getMass();
}
void RigidBodyComponent::setMass(float value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setMass(value);
}

float3 RigidBodyComponent::getCenterOfMass() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	auto pose = ((PxRigidDynamic*)instance)->getCMassLocalPose();
	return float3(pose.p.x, pose.p.y, pose.p.z);
}
void RigidBodyComponent::setCenterOfMass(const float3& value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	auto instance = (PxRigidDynamic*)this->instance;
	auto pose = instance->getCMassLocalPose();
	pose.p = PxVec3(value.x, value.y, value.z);
	instance->setCMassLocalPose(pose);
}

float3 RigidBodyComponent::getInertiaTensor() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	auto inertia = ((PxRigidDynamic*)instance)->getMassSpaceInertiaTensor();
	return float3(inertia.x, inertia.y, inertia.z);
}
void RigidBodyComponent::setInertiaTensor(const float3& value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setMassSpaceInertiaTensor(
		PxVec3(value.x, value.y, value.z));
}

void RigidBodyComponent::calcMassAndInertia(float density)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	PxRigidBodyExt::updateMassAndInertia(*((PxRigidDynamic*)instance), density);
}

//--------------------------------------------------------------------------------------------------
float RigidBodyComponent::getSleepThreshold() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getAngularDamping();
}
void RigidBodyComponent::setSleepThreshold(float value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setSleepThreshold(value);
}

//--------------------------------------------------------------------------------------------------
float RigidBodyComponent::getContactThreshold() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getContactReportThreshold();
}
void RigidBodyComponent::setContactThreshold(float value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setContactReportThreshold(value);
}

//--------------------------------------------------------------------------------------------------
void RigidBodyComponent::getSolverIterCount(
	uint32& minPosition, uint32& minVelocity) const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->getSolverIterationCounts(minPosition, minVelocity);
}
void RigidBodyComponent::setSolverIterCount(uint32 minPosition, uint32 minVelocity)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->setSolverIterationCounts(minPosition, minVelocity);
}

//--------------------------------------------------------------------------------------------------
void RigidBodyComponent::getPose(float3& position, quat& rotation) const
{
	GARDEN_ASSERT(instance);
	auto pose = ((PxRigidActor*)instance)->getGlobalPose();
	position = float3(pose.p.x, pose.p.y, pose.p.z);
	rotation = quat(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
}
void RigidBodyComponent::setPose(const float3& position, const quat& rotation)
{
	GARDEN_ASSERT(instance);
	((PxRigidActor*)instance)->setGlobalPose(
		PxTransform(position.x, position.y, position.z,
		PxQuat(rotation.x, rotation.y, rotation.z, rotation.w)));
}

//--------------------------------------------------------------------------------------------------
void RigidBodyComponent::attachShape(ID<Shape> shape)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(instance);
	auto shapeView = physicsSystem->getShapes().get(shape);
	auto pxShape = (PxShape*)shapeView->getInstance();
	((PxRigidActor*)instance)->attachShape(*pxShape);
}
void RigidBodyComponent::detachShape(ID<Shape> shape)
{
	GARDEN_ASSERT(shape);
	GARDEN_ASSERT(instance);
	auto shapeView = physicsSystem->getShapes().get(shape);
	auto pxShape = (PxShape*)shapeView->getInstance();
	((PxRigidActor*)instance)->detachShape(*pxShape);
}

uint32 RigidBodyComponent::getShapeCount() const
{
	GARDEN_ASSERT(instance);
	return ((PxRigidActor*)instance)->getNbShapes();
}
void RigidBodyComponent::getShapes(vector<ID<Shape>>& shapes)
{
	GARDEN_ASSERT(instance);
	auto instance = (PxRigidActor*)this->instance;
	auto shapeCount = instance->getNbShapes();
	vector<PxShape*> _shapes(shapeCount);
	instance->getShapes(_shapes.data(), shapeCount);
	shapes.resize(shapeCount);

	for (uint32 i = 0; i < shapeCount; i++)
		*shapes[i] = (uint32)(psize)_shapes[i]->userData;
}

//--------------------------------------------------------------------------------------------------
bool RigidBodyComponent::getLinearLockX() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
}
bool RigidBodyComponent::getLinearLockY() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
}
bool RigidBodyComponent::getLinearLockZ() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
}
void RigidBodyComponent::setLinearLockX(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_LINEAR_X, value);
}
void RigidBodyComponent::setLinearLockY(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, value);
}
void RigidBodyComponent::setLinearLockZ(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, value);
}

//--------------------------------------------------------------------------------------------------
bool RigidBodyComponent::getAngularLockX() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
}
bool RigidBodyComponent::getAngularLockY() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
}
bool RigidBodyComponent::getAngularLockZ() const
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->getRigidDynamicLockFlags() &
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
}
void RigidBodyComponent::setAngularLockX(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, value);
}
void RigidBodyComponent::setAngularLockY(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, value);
}
void RigidBodyComponent::setAngularLockZ(bool value)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	return ((PxRigidDynamic*)instance)->setRigidDynamicLockFlag(
		PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, value);
}

//--------------------------------------------------------------------------------------------------
void RigidBodyComponent::addForce(const float3& value, ForceType type)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->addForce(PxVec3(
		value.x, value.y, value.z), (PxForceMode::Enum)type);
}
void RigidBodyComponent::addTorque(const float3& value, ForceType type)
{
	GARDEN_ASSERT(!staticBody);
	GARDEN_ASSERT(instance);
	((PxRigidDynamic*)instance)->addTorque(PxVec3(
		value.x, value.y, value.z), (PxForceMode::Enum)type);
}

//--------------------------------------------------------------------------------------------------
static PxFilterFlags gardenSimulationFilterShader(
	PxFilterObjectAttributes attributes0,
	PxFilterData filterData0, 
	PxFilterObjectAttributes attributes1,
	PxFilterData filterData1,
	PxPairFlags& pairFlags,
	const void* constantBlock,
	PxU32 constantBlockSize)
{
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlags();
	}

	pairFlags = PxPairFlag::eCONTACT_DEFAULT;
	return PxFilterFlags();
}

//--------------------------------------------------------------------------------------------------
void PhysicsSystem::initialize()
{
	auto manager = getManager();
	graphicsSystem = manager->tryGet<GraphicsSystem>();
	auto logSystem = manager->tryGet<LogSystem>();
	auto threadSystem = manager->tryGet<ThreadSystem>();

	auto data = new PhysicsData();
	data->gardenErrorCallback = GardenPxErrorCallback(logSystem);
	data->gardenSimulation = GardenPxSimulation(this);
	this->data = data;

	foundation = PxCreateFoundation(PX_PHYSICS_VERSION,
		data->defaultAllocatorCallback, data->gardenErrorCallback);
	if (!foundation) throw runtime_error("Failed to create PhysX foundation.");

	#if GARDEN_DEBUG
	auto pvd = PxCreatePvd(*(PxFoundation*)foundation);
	this->pvd = pvd;

	auto transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	if (logSystem)
	{
		if (pvd->connect(*transport, PxPvdInstrumentationFlag::eALL))
			logSystem->info("Connected to the PhysX debugger.");
		else logSystem->info("PhysX debugger is not connected.");
	}
	#else
	PxPvd* pvd = nullptr;
	#endif

	auto instance = PxCreatePhysics(PX_PHYSICS_VERSION,
		*(PxFoundation*)foundation, PxTolerancesScale(), false, pvd);
	if (!instance) throw runtime_error("Failed to create PhysX instance.");
	this->instance = instance;

	 if (!PxInitExtensions(*instance, pvd))
	 	throw runtime_error("Failed to initialize PhysX extensions.");

	PxSceneDesc sceneDesc(instance->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
	sceneDesc.filterShader = gardenSimulationFilterShader;
	sceneDesc.solverType = PxSolverType::ePGS;
	// sceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
	sceneDesc.simulationEventCallback = &data->gardenSimulation;

	if (threadSystem)
	{
		data->gardenCpuDispatcher = GardenPxCpuDispatcher(
			&threadSystem->getForegroundPool());
		sceneDesc.cpuDispatcher = &data->gardenCpuDispatcher;
	}
	else
	{
		data->defaultCpuDispatcher = PxDefaultCpuDispatcherCreate(
			thread::hardware_concurrency());
		sceneDesc.cpuDispatcher = data->defaultCpuDispatcher;
	}

	auto scene = instance->createScene(sceneDesc);
	this->scene = scene;

	#if GARDEN_DEBUG
	if (pvd->isConnected())
	{
		scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f);

		auto pvdClient = scene->getScenePvdClient();
		if (pvdClient)
		{
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
		}
	}
	#endif

	ccManager = PxCreateControllerManager(*scene);
	defaultMaterial = createMaterial(0.5f, 0.5f, 0.1f);
	scratchBuffer = malloc(SCRATCH_BUFFER_SIZE);

	auto& subsystems = manager->getSubsystems<PhysicsSystem>();
	for (auto subsystem : subsystems)
	{
		auto physicsSystem = dynamic_cast<IPhysicsSystem*>(subsystem.system);
		GARDEN_ASSERT(physicsSystem);
		physicsSystem->physicsSystem = this;
	}

	#if GARDEN_EDITOR
	editor = new PhysicsEditor(this);
	#endif
}
void PhysicsSystem::terminate()
{
	shapes.clear();
	materials.clear();
	components.clear();

	((PxControllerManager*)ccManager)->release();
	((PxScene*)scene)->release();
	((PxDefaultCpuDispatcher*)((PhysicsData*)
		data)->defaultCpuDispatcher)->release();
	PxCloseExtensions();
	((PxPhysics*)instance)->release();

	#if GARDEN_DEBUG
	auto pvd = (PxPvd*)this->pvd;
	auto transport = pvd->getTransport();
	pvd->release();
	transport->release();
	#endif

	((PxFoundation*)foundation)->release();
	delete (PhysicsData*)data;
	free(scratchBuffer);

	#if GARDEN_EDITOR
	delete (PhysicsEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
void PhysicsSystem::update()
{
	auto manager = getManager();
	auto componentData = components.getData();
	auto componentOccupancy = components.getOccupancy();

	auto& subsystems = manager->getSubsystems<PhysicsSystem>();
	for (auto subsystem : subsystems)
	{
		auto physicsSystem = dynamic_cast<IPhysicsSystem*>(subsystem.system);
		physicsSystem->preSimulate();
	}

	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto component = &componentData[i];
		if (!component->physicsSystem) continue;
		if (!component->instance) component->setStatic(true);
		if (component->staticBody || !component->updatePose) continue;

		auto transformComponent = manager->get<TransformComponent>(component->entity);
		auto position = transformComponent->position;
		auto rotation = transformComponent->rotation;
		auto pxTransform = PxTransform(position.x, position.y, position.z,
			PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));
		auto rigidBody = (PxRigidActor*)component->instance;

		if (!(rigidBody->getGlobalPose() == pxTransform))
			rigidBody->setGlobalPose(pxTransform);
	}

	if (graphicsSystem)
	{
		auto graphicsSystem = (GraphicsSystem*)this->graphicsSystem;
		auto deltaTime = graphicsSystem->getDeltaTime();
		if (deltaTime == 0.0) return;

		auto scene = (PxScene*)this->scene;
		auto targetDT = 1.0 / minUpdateRate;

		if (deltaTime > targetDT)
		{
			auto count = (uint32)std::ceil(deltaTime / targetDT);
			deltaTime /= (double)count;

			for (uint32 i = 0; i < count; i ++)
			{
				for (auto subsystem : subsystems)
				{
					auto physicsSystem = dynamic_cast<IPhysicsSystem*>(subsystem.system);
					physicsSystem->simulate(deltaTime);
				}

				scene->simulate(deltaTime, nullptr,
					scratchBuffer, SCRATCH_BUFFER_SIZE);
				scene->fetchResults(true);
			}
		}
		else
		{
			scene->simulate(deltaTime, nullptr,
				scratchBuffer, SCRATCH_BUFFER_SIZE);
			scene->fetchResults(true);
		}
	}

	// TODO: multithread this.
	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto component = &componentData[i];
		if (!component->physicsSystem || component->staticBody ||
			!component->updatePose) continue;
		auto transformComponent = manager->get<TransformComponent>(component->entity);
		auto rigidBody = (PxRigidActor*)component->instance;
		auto pxTransform = rigidBody->getGlobalPose();
		transformComponent->position = float3(
			pxTransform.p.x, pxTransform.p.y, pxTransform.p.z);
		transformComponent->rotation = quat(
			pxTransform.q.x, pxTransform.q.y, pxTransform.q.z, pxTransform.q.w);
	}

	for (auto subsystem : subsystems)
	{
		auto physicsSystem = dynamic_cast<IPhysicsSystem*>(subsystem.system);
		physicsSystem->postSimulate();
	}
}

//--------------------------------------------------------------------------------------------------
type_index PhysicsSystem::getComponentType() const
{
	return typeid(RigidBodyComponent);
}
ID<Component> PhysicsSystem::createComponent(ID<Entity> entity)
{
	GARDEN_ASSERT(getManager()->has<TransformComponent>(entity));
	auto component = components.create();
	auto componentView = components.get(component);
	componentView->physicsSystem = this;
	componentView->entity = entity;
	return ID<Component>(component);
}
void PhysicsSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<RigidBodyComponent>(instance));
}
View<Component> PhysicsSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<RigidBodyComponent>(instance)));
}
void PhysicsSystem::disposeComponents()
{
	components.dispose();
	shapes.dispose();
	materials.dispose();
}

//--------------------------------------------------------------------------------------------------
ID<Material> PhysicsSystem::createMaterial(float staticFriction,
	float dynamicFriction, float restitution)
{
	auto component = materials.create();
	auto componentView = materials.get(component);
	auto physics = (PxPhysics*)this->instance;
	auto material = physics->createMaterial(staticFriction, dynamicFriction, restitution);
	componentView->instance = material;
	return component;
}
View<Material> PhysicsSystem::get(ID<Material> material) const
{
	return materials.get(material);
}
void PhysicsSystem::destroy(ID<Material> material)
{
	materials.destroy(material);
}

//--------------------------------------------------------------------------------------------------
ID<Shape> PhysicsSystem::createShape(Shape::Type type,
	ID<Material> material, void* geometry)
{
	GARDEN_ASSERT(material);
	GARDEN_ASSERT(geometry);
	auto component = shapes.create();
	auto componentView = shapes.get(component);
	auto physics = (PxPhysics*)this->instance;
	auto materialView = materials.get(material);
	auto pxMaterial = (PxMaterial*)materialView->getInstance();
	auto shape = physics->createShape(*((PxGeometry*)geometry), *pxMaterial);
	shape->userData = (void*)(psize)*component;
	componentView->physicsSystem = this;
	componentView->instance = shape;
	componentView->material = material;
	componentView->type = type;
	return component;
}
ID<Shape> PhysicsSystem::createBoxShape(ID<Material> material, const float3& halfSize)
{
	GARDEN_ASSERT(material);
	auto geometry = PxBoxGeometry(halfSize.x, halfSize.y, halfSize.z);
	return createShape(Shape::Type::Cube, material, (PxGeometry*)&geometry);
}
ID<Shape> PhysicsSystem::createSphereShape(ID<Material> material, float radius)
{
	GARDEN_ASSERT(material);
	auto geometry = PxSphereGeometry(radius);
	return createShape(Shape::Type::Sphere, material, (PxGeometry*)&geometry);
}
ID<Shape> PhysicsSystem::createPlaneShape(ID<Material> material)
{
	GARDEN_ASSERT(material);
	auto geometry = PxPlaneGeometry();
	return createShape(Shape::Type::Plane, material, (PxGeometry*)&geometry);
}
ID<Shape> PhysicsSystem::createCapsuleShape(
	ID<Material> material, float radius, float halfHeight)
{
	GARDEN_ASSERT(material);
	auto geometry = PxCapsuleGeometry(radius, halfHeight);
	return createShape(Shape::Type::Capsule, material, (PxGeometry*)&geometry);
}
ID<Shape> PhysicsSystem::createCustomShape(ID<Material> material, void* callbacks)
{
	GARDEN_ASSERT(material);
	auto geometry = PxCustomGeometry(*((PxCustomGeometry::Callbacks*)callbacks));
	return createShape(Shape::Type::Custom, material, (PxGeometry*)&geometry);
}
View<Shape> PhysicsSystem::get(ID<Shape> shape) const
{
	return shapes.get(shape);
}
void PhysicsSystem::destroy(ID<Shape> shape)
{
	shapes.destroy(shape);
}
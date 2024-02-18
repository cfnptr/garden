//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

// TODO: refactor this

/*
#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "math/quaternion.hpp"

namespace garden
{
	class PhysicsSystem;
};

namespace
{
	class GardenPxSimulation;
};

namespace garden::physics
{

using namespace math;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
struct Material final : public Component
{
private:
	void* instance = nullptr;

	bool destroy();

	friend class garden::PhysicsSystem;
	friend class LinearPool<Material>;
public:
	void* getInstance() noexcept { return instance; }
	
	float getStaticFriction() const;
	void setStaticFriction(float value);

	float getDynamicFriction() const;
	void setDynamicFriction(float value);

	float getRestitution() const;
	void setRestitution(float value);
};

//--------------------------------------------------------------------------------------------------
struct Shape final : public Component
{
public:
	enum class Type : uint8
	{
		Cube, Sphere, Plane, Capsule, Custom, Count,
	};
private:
	PhysicsSystem* physicsSystem = nullptr;
	void* instance = nullptr;
	ID<Material> material = {};
	Type type = {};
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;

	bool destroy();

	void setGeometry(Shape::Type type, void* geometry);
	friend class garden::PhysicsSystem;
	friend class LinearPool<Shape>;
public:
	void* getInstance() noexcept { return instance; }
	Type getType() const noexcept { return type; }

	ID<Material> getMaterial() const noexcept { return material; }
	void setMaterial(ID<Material> material);
	
	void getPose(float3& position, quat& rotation) const;
	void setPose(const float3& position,
		const quat& rotation = quat::identity);
	
	bool isTrigger() const;
	void setTrigger(bool value);

	void setBoxGeometry(const float3& halfSize);
	void setSphereGeometry(float radius);
	void setPlaneGeometry();
	void setCapsuleGeometry(float radius, float halfHeight);
	void setCustomGeometry(void* callbacks);

	float getContactOffset() const;
	void setContactOffset(float value);

	float getRestOffset() const;
	void setRestOffset(float value);
};

} // namespace garden::physics

namespace garden
{

using namespace ecsm;
using namespace garden::physics;

//--------------------------------------------------------------------------------------------------
struct RigidBodyComponent final : public Component
{
public:
	enum class ForceType : uint8
	{
		Force,			// mass * length / time^2
		Impulse, 		// mass * length / time
		VeolcityChange,	// length / time
		Accceleration, 	// length / time^2
		Count
	};
private:
	PhysicsSystem* physicsSystem = nullptr;
	void* instance = nullptr;
	uint16 _alignment = 0;
	bool staticBody = false;

	bool destroy();

	friend class PhysicsSystem;
	friend class LinearPool<RigidBodyComponent>;
public:
	bool updatePose = true;

	void* getInstance() noexcept { return instance; }

	bool isStatic() const noexcept { return staticBody; }
	void setStatic(bool isStatic);

	bool isSleeping() const;
	void putToSleep();
	void wakeUp();

	float getLinearDamping() const;
	void setLinearDamping(float value);

	float getAngularDamping() const;
	void setAngularDamping(float value);

	float3 getLinearVelocity() const;
	void setLinearVelocity(const float3& value);

	float3 getAngularVelocity() const;
	void setAngularVelocity(const float3& value);

	float getMass() const;
	void setMass(float value);

	float3 getCenterOfMass() const;
	void setCenterOfMass(const float3& value);
	
	float3 getInertiaTensor() const;
	void setInertiaTensor(const float3& value);

	void calcMassAndInertia(float density = 1.0f);

	float getSleepThreshold() const;
	void setSleepThreshold(float value);

	float getContactThreshold() const;
	void setContactThreshold(float value);

	void getSolverIterCount(uint32& minPosition, uint32& minVelocity) const;
	void setSolverIterCount(uint32 minPosition, uint32 minVelocity);

	void getPose(float3& position, quat& rotation) const;
	void setPose(const float3& position,
		const quat& rotation = quat::identity);

	void attachShape(ID<Shape> shape);
	void detachShape(ID<Shape> shape);

	uint32 getShapeCount() const;
	void getShapes(vector<ID<Shape>>& shapes);

	bool getLinearLockX() const;
	bool getLinearLockY() const;
	bool getLinearLockZ() const;
	void setLinearLockX(bool value);
	void setLinearLockY(bool value);
	void setLinearLockZ(bool value);

	bool getAngularLockX() const;
	bool getAngularLockY() const;
	bool getAngularLockZ() const;
	void setAngularLockX(bool value);
	void setAngularLockY(bool value);
	void setAngularLockZ(bool value);

	void addForce(const float3& value, ForceType type = ForceType::Force);
	void addTorque(const float3& value, ForceType type = ForceType::Force);
};

//--------------------------------------------------------------------------------------------------
class IPhysicsSystem
{
public:
	struct TriggerData
	{
		ID<Entity> triggerEntity = {};
		ID<Entity> otherEntity = {};
		ID<Shape> triggerShape = {};
		ID<Shape> otherShape = {};
		bool isEntered = false;
	};
private:
	PhysicsSystem* physicsSystem = nullptr;
protected:
	virtual void preSimulate() { }
	virtual void simulate(double deltaTime) { }
	virtual void postSimulate() { }
	virtual void onTrigger(const TriggerData& data) { }

	friend class PhysicsSystem;
	friend class ::GardenPxSimulation;
public:
	PhysicsSystem* getPhysicsSystem() noexcept
	{
		GARDEN_ASSERT(physicsSystem);
		return physicsSystem;
	}
	const PhysicsSystem* getPhysicsSystem() const noexcept
	{
		GARDEN_ASSERT(physicsSystem);
		return physicsSystem;
	}
};

//--------------------------------------------------------------------------------------------------
class PhysicsSystem final : public System
{
	LinearPool<RigidBodyComponent> components;
	LinearPool<Material> materials;
	LinearPool<Shape> shapes;
	void* graphicsSystem = nullptr;
	void* data = nullptr;
	void* foundation = nullptr;
	void* instance = nullptr;
	void* scene = nullptr;
	void* ccManager = nullptr;
	uint8* scratchBuffer = nullptr;
	ID<Material> defaultMaterial = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	void* pvd = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void update() final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	ID<Shape> createShape(Shape::Type type, ID<Material> material, void* geometry);
	friend class ecsm::Manager;
public:
	float minUpdateRate = 30.0f;

	ID<Material> getDefaultMaterial()
		const noexcept { return defaultMaterial; }
	const LinearPool<RigidBodyComponent>& getComponents()
		const noexcept { return components; }
	const LinearPool<Material>& getMaterials()
		const noexcept { return materials; }
	const LinearPool<Shape>& getShapes()
		const noexcept { return shapes; }
	void* getFoundation() noexcept { return foundation; }
	void* getInstance() noexcept { return instance; }
	void* getScene() noexcept { return scene; }

	ID<Material> createMaterial(float staticFriction,
		float dynamicFriction, float restitution);
	View<Material> get(ID<Material> material) const;
	void destroy(ID<Material> material);

	ID<Shape> createBoxShape(ID<Material> material, const float3& halfSize);
	ID<Shape> createSphereShape(ID<Material> material, float radius);
	ID<Shape> createPlaneShape(ID<Material> material);
	ID<Shape> createCapsuleShape(ID<Material> material, float radius, float halfHeight);
	ID<Shape> createCustomShape(ID<Material> material, void* callbacks);
	View<Shape> get(ID<Shape> shape) const;
	void destroy(ID<Shape> shape);
};

} // namespace garden
*/
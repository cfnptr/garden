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

#pragma once
#include "garden/hash.hpp"
#include "garden/animate.hpp"

namespace garden
{
	class PhysicsSystem;
	struct RigidbodyComponent;
}

namespace garden::physics
{

using namespace math;
using namespace ecsm;

/***********************************************************************************************************************
 * @brief Motion type of a physics body.
 */
enum class MotionType : uint8
{
	Static,    /**< Non movable */
	Kinematic, /**< Movable using velocities only, does not respond to forces */
	Dynamic,   /**< Responds to forces as a normal physics object */
	Count      /**< Physics body motion type count */
};

/**
 * @brief Category of a collision volume shape.
 */
enum class ShapeType : uint8
{
	Convex,
	Compound,
	Decorated,
	Mesh,
	HeightField,
	SoftBody,

	// User defined shapes
	User1,
	User2,
	User3,
	User4,
	Count
};

/***********************************************************************************************************************
 * @brief Collision volume shape sub type.
 */
enum class ShapeSubType : uint8
{
	// Convex shapes
	Sphere,
	Box,
	Triangle,
	Capsule,
	TaperedCapsule,
	Cylinder,
	ConvexHull,

	// Compound shapes
	StaticCompound,
	MutableCompound,

	// Decorated shapes
	RotatedTranslated,
	Scaled,
	OffsetCenterOfMass,

	// Other shapes
	Mesh,
	HeightField,
	SoftBody,

	// User defined shapes
	User1,
	User2,
	User3,
	User4,
	User5,
	User6,
	User7,
	User8,

	// User defined convex shapes
	UserConvex1,
	UserConvex2,
	UserConvex3,
	UserConvex4,
	UserConvex5,
	UserConvex6,
	UserConvex7,
	UserConvex8,
	Count,
};

/***********************************************************************************************************************
 * @brief Collision volume of a physics body.
 */
struct Shape final
{
private:
	void* instance = nullptr;

	Shape() = default;
	Shape(void* _instance) : instance(_instance) { }

	bool destroy();

	friend class PhysicsSystem;
	friend class LinearPool<Shape>;
	friend struct RigidbodyComponent;
public:
	/**
	 * @brief Return category of the collision volume shape.
	 */
	ShapeType getType() const;
	/**
	 * @brief Collision volume shape sub type.
	 */
	ShapeSubType getSubType() const;

	/**
	 * @brief Returns box shape half extent.
	 */
	float3 getBoxHalfExtent() const;
	/**
	 * @brief Returns box shape convex radius.
	 */
	float getBoxConvexRadius() const;

	/**
	 * @brief Returns current shape reference count.
	 */
	uint64 getRefCount() const;
	/**
	 * @brief Returns true if this is last shape reference.
	 */
	bool isLastRef() const;
};

} // garden::physics

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace physics;

/***********************************************************************************************************************
 * @brief A rigid body that can be simulated using the physics system.
 */
struct RigidbodyComponent final : public Component
{
private:
	uint32 instance = 0;
	ID<Shape> shape = {};
	MotionType motionType = {};
	bool inSimulation = true;
	uint16 _alignment = 0;

	bool destroy();

	friend class PhysicsSystem;
	friend class LinearPool<RigidbodyComponent>;
public:
	/**
	 * @brief Returns motion type of the rigidbody.
	 */
	MotionType getMotionType() const noexcept { return motionType; }
	/**
	 * @brief Sets motion type of the rigidbody.
	 * @note Rigidbody should allowDynamicOrKinematic to set dynamic or kinmatic motion type.
	 * 
	 * @param motionType target motion type
	 * @param activate is rigidbody should be activated
	 */
	void setMotionType(MotionType motionType, bool activate = true);

	/**
	 * @brief Returns rigidbody shape instance.
	 */
	ID<Shape> getShape() const noexcept { return shape; }
	/**
	 * @brief Sets rigidbody shape instance. (Expensive operation!)
	 * 
	 * @details
	 * It also creates rigidbody instance if it doesn't already exists, 
	 * and adds it to the physics simulation if simulating set to true.
	 * 
	 * @param shape target shape instance or null
	 * @param activate is rigidbody should be activated
	 * @param allowDynamicOrKinematic allow to change static motion type
	 */
	void setShape(ID<Shape> shape, bool activate = true, bool allowDynamicOrKinematic = false);

	/**
	 * @brief Allow to change static motion type to the dynamic or kinematic.
	 */
	bool canBeKinematicOrDynamic() const;

	/**
	 * @brief Is rigidbody is currently actively simulating (true) or sleeping (false).
	 * 
	 * @details 
	 * When a rigidbody is sleeping, it can still detect collisions with other objects that are not sleeping, 
	 * but it will not move or otherwise participate in the simulation to conserve CPU cycles. Sleeping 
	 * bodies wake up automatically when they're in contact with non-sleeping objects or they can be explicitly 
	 * woken through an @ref activate() function call.
	 */
	bool isActive() const;

	/**
	 * @brief Wakes up rigidbody if it's sleeping.
	 */
	void activate();
	/**
	 * @brief Puts rigidbody to a sleep.
	 */
	void deactivate();

	/**
	 * @brief Returns rigidbody position in the phyics simulation world.
	 */
	float3 getPosition() const;
	/**
	 * @brief Sets rigidbody position in the phyics simulation world.
	 * 
	 * @param[in] position target rigidbody position
	 * @param activate is rigidbody should be activated
	 */
	void setPosition(const float3& position, bool activate = true);

	/**
	 * @brief Returns rigidbody rotation in the phyics simulation world.
	 */
	quat getRotation() const;
	/**
	 * @brief Sets rigidbody rotation in the phyics simulation world.
	 *
	 * @param[in] rotation target rigidbody rotation
	 * @param activate is rigidbody should be activated
	 */
	void setRotation(const quat& rotation, bool activate = true);

	/**
	 * @brief Returns rigidbody position and rotation in the phyics simulation world.
	 * 
	 * @param[out] position rigidbody position
	 * @param[out] rotation rigidbody rotation
	 */
	void getPosAndRot(float3& position, quat& rotation) const;
	/**
	 * @brief Sets rigidbody position and rotation in the phyics simulation world.
	 *
	 * @param[in] position target rigidbody position
	 * @param[in] rotation target rigidbody rotation
	 * @param activate is rigidbody should be activated
	 */
	void setPosAndRot(const float3& position, const quat& rotation, bool activate = true);
};

class PhysicsSystem final : public System, public ISerializable
{
public:
	struct Properties final
	{
		uint32 tempBufferSize = 10 * 1024 * 1024; // 10mb
		uint32 maxRigidbodies = 65536;
		uint32 bodyMutexCount = 0; // 0 = auto
		uint32 maxBodyPairs = 65536;
		uint32 maxContactConstraints = 10240;
	};
private:
	LinearPool<RigidbodyComponent> components;
	LinearPool<Shape> shapes;
	map<Hash128, ID<Shape>> sharedBoxShapes;
	void* tempAllocator = nullptr;
	void* jobSystem = nullptr;
	void* bpLayerInterface = nullptr;
	void* objVsBpLayerFilter = nullptr;
	void* objVsObjLayerFilter = nullptr;
	void* physicsInstance = nullptr;
	void* bodyInterface = nullptr;
	const void* lockInterface = nullptr;
	float deltaTimeAccum = 0.0f;

	static PhysicsSystem* instance;

	/**
	 * @brief Creates a new physics system instance.
	 * @param properties target pshysics simulation properties
	 */
	PhysicsSystem(const Properties& properties = {});
	/**
	 * @brief Destroy physics system instance.
	 */
	~PhysicsSystem() final;

	void preInit();
	void postInit();
	void simulate();

	void updateInSimulation();
	void interpolateResult();

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	
	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;
	
	friend class ecsm::Manager;
	friend struct RigidbodyComponent;
public:
	/**
	 * @brief Collision step count during simulation step.
	 */
	int32 collisionSteps = 1;
	/**
	 * @brief Simulation update count per second.
	 */
	uint16 simulationRate = 60;

	/**
	 * @brief Returns rigidbody component pool.
	 */
	const LinearPool<RigidbodyComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns physics shape pool.
	 */
	const LinearPool<Shape>& getShapes() const noexcept { return shapes; }

	/**
	 * @brief Creates a new box shape instance.
	 * 
	 * @details
	 * Internally the convex radius will be subtracted from the half 
	 * extent so the total box will not grow with the convex radius.
	 * 
	 * @param[in] halfExtent half edge length
	 * @param convexRadius box convex radius
	 */
	ID<Shape> createBoxShape(const float3& halfExtent, float convexRadius = 0.05f);
	/**
	 * @brief Creates a new shared box shape instance.
	 * @details See the @ref createBoxShape().
	 *
	 * @param[in] halfExtent half edge length
	 * @param convexRadius box convex radius
	 */
	ID<Shape> createSharedBoxShape(const float3& halfExtent, float convexRadius = 0.05f);

	/**
	 * @brief Returns shape instance view.
	 * @param shape target shape instance
	 */
	View<Shape> get(ID<Shape> shape) const noexcept { return shapes.get(shape); }

	/**
	 * Destroys shape instance.
	 * @param shape target shape instance or null
	 */
	void destroy(ID<Shape> shape) { shapes.destroy(shape); }
	/**
	 * Destroys shared shape if it's the last one.
	 * @param shape target shape instance or null
	 */
	void destroyShared(ID<Shape> shape);

	/**
	 * @brief Improves collision detection performance. (Expensive operation!)
	 */
	void optimizeBroadPhase();

	/**
	 * @brief Returns true if entity has rigidbody component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity rigidbody component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<RigidbodyComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity rigidbody component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<RigidbodyComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns physics system instance.
	 */
	static PhysicsSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
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
#include "math/flags.hpp"

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
 * @brief Which degrees of freedom physics body has. (can be used to limit simulation to 2D)
 */
enum class AllowedDOF : uint8
{
	None = 0b000000,                                   /**< No degrees of freedom are allowed. Note that this is not valid and will crash. Use a static body instead. */
	All = 0b111111,                                    /**< All degrees of freedom are allowed */
	TranslationX = 0b000001,                           /**< Body can move in world space X axis */
	TranslationY = 0b000010,                           /**< Body can move in world space Y axis */
	TranslationZ = 0b000100,                           /**< Body can move in world space Z axis */
	RotationX = 0b001000,                              /**< Body can rotate around world space X axis  */
	RotationY = 0b010000,                              /**< Body can rotate around world space Y axis */
	RotationZ = 0b100000,                              /**< Body can rotate around world space Z axis */
	Plane2D = TranslationX | TranslationY | RotationZ, /**< Body can only move in X and Y axis and rotate around Z axis */
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

/**
 * @brief Physics body event type.
 */
enum class BodyEvent : uint8
{
	Activated,        /**< Called whenever a body is activated */
	Deactivated,      /**< Called whenever a body is deactivated */
	ContactAdded,     /**< Called whenever a new contact point is detected */
	ContactPersisted, /**< Called whenever a contact is detected that was also detected last update */
	ContactRemoved,   /**< Called whenever a contact was detected last update but is not detected anymore */
	Count             /**< Physics body event count */
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

DECLARE_ENUM_CLASS_FLAG_OPERATORS(AllowedDOF)

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
public:
	typedef std::function<void(ID<Entity> thisEntity, ID<Entity> otherEntity)> Callback;

	struct Listener final
	{
		Callback callback = nullptr;
		BodyEvent eventType = {};
	private:
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
	};
private:
	void* instance = nullptr;
	vector<Listener> listeners;
	ID<Shape> shape = {};
	float3 lastPosition = float3(0.0f);
	quat lastRotation = quat::identity;
	MotionType motionType = {};
	AllowedDOF allowedDOF = {};
	bool inSimulation = true;
	uint8 _alignment = 0;

	bool destroy();

	friend class PhysicsSystem;
	friend class LinearPool<RigidbodyComponent>;
public:
	/**
	 * @brief Returns rigidbody events listener array,
	 */
	const vector<Listener>& getListeners() const noexcept { return listeners; }

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
	 * and adds it to the physics simulation if transform is active.
	 * 
	 * @param shape target shape instance or null
	 * @param activate is rigidbody should be activated
	 * @param allowDynamicOrKinematic allow to change static motion type
	 * @param isSensor is rigidbody should trigger collision events
	 * @param allowedDOW which degrees of freedom rigidbody has
	 */
	void setShape(ID<Shape> shape, bool activate = true, bool allowDynamicOrKinematic = false, 
		bool isSensor = false, AllowedDOF allowedDOF = AllowedDOF::All);

	/**
	 * @brief Allow to change static motion type to the dynamic or kinematic.
	 */
	bool canBeKinematicOrDynamic() const noexcept;
	/**
	 * @brief Returns which degrees of freedom rigidbody has.
	 */
	AllowedDOF getAllowedDOF() const noexcept { return allowedDOF; }

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

	/**
	 * @brief Set velocity of rigidbody such that it will be positioned at position/rotation in deltaTime.
	 * @note It will activate body if needed.
	 *
	 * @param[in] position target rigidbody position
	 * @param[in] rotation target rigidbody rotation
	 * @param deltaTime target delta time in seconds
	 */
	void moveKinematic(const float3& position, const quat& rotation, float deltaTime);

	/**
	 * @brief Is this rigidbody reports contacts with other rigidbodies. 
	 * 
	 * @details
	 * Any detected penetrations will however not be resolved. Sensors can be used to implement triggers that 
	 * detect when an object enters their area. The cheapest sensor has a Static motion type. This type of 
	 * sensor will only detect active bodies entering their area. As soon as a body goes to sleep, the contact 
	 * will be lost. Note that you can still move a Static sensor around using position and rotation setters.
	 * If you make a sensor Dynamic or Kinematic and activate them, the sensor will be able to detect collisions 
	 * with sleeping bodies too. An active sensor will never go to sleep automatically.
	 */
	bool isSensor() const;
	/**
	 * @brief Sets rigidbody sensor state.
	 * @details See the @ref isSensor().
	 */
	void setSensor(bool isSensor);
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
	struct Event final
	{
		uint32 data1 = 0;
		uint32 data2 = 0;
		BodyEvent eventType = {};
	private:
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
	};
private:
	LinearPool<RigidbodyComponent> components;
	LinearPool<Shape> shapes;
	map<Hash128, ID<Shape>> sharedBoxShapes;
	vector<Event> bodyEvents;
	void* tempAllocator = nullptr;
	void* jobSystem = nullptr;
	void* bpLayerInterface = nullptr;
	void* objVsBpLayerFilter = nullptr;
	void* objVsObjLayerFilter = nullptr;
	void* physicsInstance = nullptr;
	void* bodyListener = nullptr;
	void* contactListener = nullptr;
	void* bodyInterface = nullptr;
	const void* lockInterface = nullptr;
	mutex bodyEventLocker;
	string valueStringCache;
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
	void postDeinit();
	void simulate();

	void prepareSimulate();
	void processSimulate();
	void interpolateResult(float t);

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
// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

/***********************************************************************************************************************
 * @file
 * @brief Rigid body physics simulation functions.
 * @details See the Jolt physics docs: https://jrouwe.github.io/JoltPhysics/index.html
 */

#pragma once
#include "garden/hash.hpp"
#include "garden/animate.hpp"
#include "garden/network.hpp"

#include "math/flags.hpp"
#include "math/sphere.hpp"

namespace garden
{
	struct RigidbodyComponent;
	class PhysicsSystem;
	struct CharacterComponent;
	class CharacterSystem;
}

namespace garden::physics
{

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
	None         = 0b000000, /**< No degrees of freedom are allowed. (Use a static body instead) */
	All          = 0b111111, /**< All degrees of freedom are allowed */
	TranslationX = 0b000001, /**< Body can move in world space X axis */
	TranslationY = 0b000010, /**< Body can move in world space Y axis */
	TranslationZ = 0b000100, /**< Body can move in world space Z axis */
	RotationX    = 0b001000, /**< Body can rotate around world space X axis  */
	RotationY    = 0b010000, /**< Body can rotate around world space Y axis */
	RotationZ    = 0b100000, /**< Body can rotate around world space Z axis */
	Plane2D = TranslationX | TranslationY | RotationZ, /**< Body can only move in X and Y axis and rotate around Z axis */
};

/**
 * @brief Category of a collision volume shape.
 */
enum class ShapeType : uint8
{
	Convex, /**< Box, sphere, capsule, tapered capsule, cylinder, triangle */
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
	Entered,          /**< Called whenever a new contact point is detected */
	Stayed,           /**< Called whenever a contact is detected that was also detected last update */
	Exited,           /**< Called whenever a contact was detected last update but is not detected anymore */
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

	// Other shapes
	Plane,
	TaperedCylinder,
	Empty,
	Count
};

/***********************************************************************************************************************
 * @brief Rigid body constraint type. (Connection between two bodies)
 */
enum class ConstraintType : uint8
{
	Fixed, Point, Count
};
/**
 * @brief Rigid body constraint type names.
 */
constexpr const char* constraintTypeNames[(uint8)ConstraintType::Count] =
{
	"Fixed", "Point"
};
/**
 * @brief Returns constraint type name string.
 * @param constraintType target constraint type
 */
static string_view toString(ConstraintType constraintType) noexcept
{
	GARDEN_ASSERT(constraintType < ConstraintType::Count);
	return constraintTypeNames[(psize)constraintType];
}
/**
 * @brief Returns constraint type from name string.
 *
 * @param name target constraint type name string
 * @param[out] constraintType constraint type
 */
static bool toConstraintType(string_view name, ConstraintType& constraintType) noexcept
{
	if (name == "Fixed") { constraintType = ConstraintType::Fixed; return true; }
	if (name == "Point") { constraintType = ConstraintType::Point; return true; }
	return false;
}

/***********************************************************************************************************************
 * @brief Determines which other objects rigid body can collide with.
 */
enum class CollisionLayer : int8
{
	Auto         = -1,
	NonMoving    = 0,
	Moving       = 1,
	Sensor       = 2, /**< Sensors only collide with Moving objects */
	HqDebris     = 3, /**< High quality debris collides with Moving and NonMoving but not with any debris */
	LqDebris     = 4, /**< Low quality debris only collides with NonMoving */
	DefaultCount = 5,
	// You can add your own object layers
};

/**
 * @brief Determines which other objects rigid body can collide with.
 * 
 * @details
 * An object layer can be mapped to a broadphase layer. Objects with the same broadphase layer will end up in the same 
 * sub structure (usually a tree) of the broadphase. When there are many layers, this reduces the total amount of sub 
 * structures the broad phase needs to manage. Usually you want objects that don't collide with each other in different 
 * broad phase layers, but there could be exceptions if objects layers only contain a minor amount of objects so it is 
 * not beneficial to give each layer its own sub structure in the broadphase.
 */
enum class BroadPhaseLayer : int8
{
	Auto         = -1,
	NonMoving    =  0,
	Moving       =  1,
	Sensor       =  2,
	LqDebris     =  3, // low quality
	DefaultCount =  4,
	// You can add your own broad phase layers
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(AllowedDOF)

/***********************************************************************************************************************
 * @brief Collision volume of a physics body.
 */
struct Shape final
{
private:
	void* instance = nullptr;

	Shape() noexcept = default;
	Shape(void* instance) noexcept : instance(instance) { }

	bool destroy();

	friend class LinearPool<Shape>;
	friend class garden::PhysicsSystem;
	friend struct garden::RigidbodyComponent;
	friend struct garden::CharacterComponent;
public:
	/**
	 * @brief Return category of the collision volume shape.
	 */
	ShapeType getType() const;
	/**
	 * @brief Returns collision volume shape sub type.
	 */
	ShapeSubType getSubType() const;

	/**
	 * @brief Returns shape center of mass.
	 */
	f32x4 getCenterOfMass() const;
	/**
	 * @brief Returns shape volume. (m^3)
	 * @note Compound shapes volume may be incorrect since child shapes can overlap which is not accounted for.
	 */
	float getVolume() const;
	/**
	 * @brief Returns shape mass properties.
	 * 
	 * @param[out] mass a mass of the shape (kg)
	 * @param[out] inertia an inertia tensor of the shape (kg / m^2)
	 */
	void getMassProperties(float& mass, f32x4x4& inertia) const;
	
	/**
	 * @brief Returns shape density. (kg / m^3)
	 */
	float getDensity() const;
	// TODO: allow to set density. But we should then invalidate shared shapes.

	/**
	 * @brief Returns box shape half extent.
	 */
	f32x4 getBoxHalfExtent() const;
	/**
	 * @brief Returns box shape convex radius.
	 */
	float getBoxConvexRadius() const;

	/**
	 * @brief Returns sphere shape radius.
	 */
	float getSphereRadius() const;

	/**
	 * @brief Returns capsule shape half height.
	 */
	float getCapsuleHalfHeight() const;
	/**
	 * @brief Returns capsule shape radius.
	 */
	float getCapsuleRadius() const;

	/**
	 * @brief Returns decorated shape inner instance.
	 */
	ID<Shape> getInnerShape() const;
	/**
	 * @brief Returns rotated/translated shape position.
	 */
	f32x4 getPosition() const;
	/**
	 * @brief Returns rotated/translated shape rotation.
	 */
	quat getRotation() const;
	/**
	 * @brief Returns rotated/translated shape position and rotation.
	 */
	void getPosAndRot(f32x4& position, quat& rotation) const;

	/**
	 * @brief Returns current shape reference count.
	 */
	uint64 getRefCount() const;
	/**
	 * @brief Is this the last shape reference.
	 */
	bool isLastRef() const;

	/**
	 * @brief Retruns shape internal instance.
	 * @warning Use only if you know what you are doing!
	 */
	void* getInstance() const noexcept { return instance; }
};

/**
 * @brief Shape hit information container.
 */
struct ShapeHit
{
	ID<Entity> entity = {}; /**< Hit shape entity instance. */
	uint32 subShapeID = 0;  /**< Hit shape sub-shape ID. */
};
/**
 * @brief Ray cast hit information container.
 */
struct RayCastHit : public ShapeHit
{
	f32x4 surfacePoint = f32x4::zero;  /**< Hit shape surface point in the world space. */
	f32x4 surfaceNormal = f32x4::zero; /**< Hit shape surface normal vector. */
};

} // namespace garden::physics

namespace garden
{

using namespace garden::physics;

/***********************************************************************************************************************
 * @brief A rigid body that can be simulated using the physics system.
 */
struct RigidbodyComponent final : public Component
{
public:
	/**
	 * @brief Constraints allows to connect rigid bodies to each other.
	 */
	struct Constraint
	{
		void* instance = nullptr;
		ID<Entity> otherBody = {}; 
		ConstraintType type = {};
	};
private:
	ID<Shape> shape = {};
	f32x4 lastPosition = f32x4::zero;
	quat lastRotation = quat::identity;
	vector<Constraint> constraints;
	void* instance = nullptr;
	uint64 uid = 0;
	bool inSimulation = true;
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;

	friend class PhysicsSystem;
	friend struct CharacterComponent;
public:
	string eventListener = ""; /**< Rigidbody events listener name. */

	/**
	 * @brief Returns rigidbody constraint array.
	 */
	const vector<Constraint>& getConstraints() const noexcept { return constraints; }

	/*******************************************************************************************************************
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
	 * @param motionType rigidbody motion type
	 * @param collisionLayer rigidbody collision layer index
	 * @param activate is rigidbody should be activated
	 * @param allowDynamicOrKinematic allow to change static motion type
	 * @param allowedDOW which degrees of freedom rigidbody has
	 */
	void setShape(ID<Shape> shape, 
		MotionType motionType = MotionType::Static, int32 collisionLayer = (int32)CollisionLayer::Auto,
		bool activate = true, bool allowDynamicOrKinematic = false, AllowedDOF allowedDOF = AllowedDOF::All);

	/**
	 * @brief Notifies all physics systems that a shape has changed. (Usable for custom shapes)
	 * 
	 * @param previousCenterOfMass center of mass of the shape before the alterations
	 * @param updateMassProperties are mass and inertia tensor should be recalculated
	 * @param activate is rigidbody should be activated
	 */
	void notifyShapeChanged(f32x4 previousCenterOfMass, bool updateMassProperties, bool activate);

	/**
	 * @brief Returns true if rigidbody should be used in the physics simulation system.
	 * @note This value is controlled by the transform isActive if transform component added to the entity.
	 */
	bool isInSimulation() const noexcept { return inSimulation; }
	/**
	 * @brief Adds or removes rigidbody to/from the physics simulation system.
	 * @details See the isInSimulation() for details.
	 * @param inSimulation add or remove from the simulation
	 */
	void setInSimulation(bool inSimulation);

	/**
	 * @brief Allow to change static motion type to the dynamic or kinematic.
	 */
	bool canBeKinematicOrDynamic() const;
	/**
	 * @brief Returns which degrees of freedom rigidbody has.
	 */
	AllowedDOF getAllowedDOF() const;
	/**
	 * @brief Returns in which broad phase sub-tree the rigidbody is placed.
	 */
	uint8 getBroadPhaseLayer() const;

	/**
	 * @brief Returns motion type of the rigidbody.
	 */
	MotionType getMotionType() const;
	/**
	 * @brief Sets motion type of the rigidbody.
	 * @note Rigidbody should allowDynamicOrKinematic to set dynamic or kinematic motion type.
	 *
	 * @param motionType target motion type
	 * @param activate is rigidbody should be activated
	 */
	void setMotionType(MotionType motionType, bool activate = true);

	/**
	 * @brief Returns rigidbody collision layer index.
	 * @details It determines which other objects it collides with.
	 */
	uint16 getCollisionLayer() const;
	/**
	 * @brief Sets rigidbody collision layer index.
	 * @details It determines which other objects it collides with.
	 * @param collisionLayer target collision layer
	 */
	void setCollisionLayer(int32 collisionLayer = (int32)CollisionLayer::Auto);

	/*******************************************************************************************************************
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
	 * @details See the @ref isActive()
	 */
	void activate();
	/**
	 * @brief Puts rigidbody to a sleep.
	 * @details See the @ref isActive()
	 */
	void deactivate();

	/**
	 * @brief Is rigidbody reports contacts with other rigidbodies.
	 *
	 * @details
	 * Any detected penetrations will however not be resolved. Sensors can be used to implement triggers that
	 * detect when an object enters their area. The cheapest sensor has a Static motion type. This type of
	 * sensor will only detect active bodies entering their area. As soon as a rigidbody goes to sleep, the contact
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

	/**
	 * @brief Can kinematic objects collide with other kinematic or static objects.
	 * @details See the @ref setKinematicVsStatic().
	 */
	bool isKinematicVsStatic() const;
	/**
	 * @brief Sets if kinematic objects can collide with other kinematic or static objects.
	 * @warning Turning this on can be CPU intensive as much more collision detection work will be done.
	 */
	void setKinematicVsStatic(bool isKinematicVsStatic);

	/*******************************************************************************************************************
	 * @brief Returns rigidbody position in the physics simulation world.
	 */
	f32x4 getPosition() const;
	/**
	 * @brief Sets rigidbody position in the physics simulation world.
	 * 
	 * @param position target rigidbody position
	 * @param activate is rigidbody should be activated
	 */
	void setPosition(f32x4 position, bool activate = true);

	/**
	 * @brief Returns rigidbody rotation in the physics simulation world.
	 */
	quat getRotation() const;
	/**
	 * @brief Sets rigidbody rotation in the physics simulation world.
	 *
	 * @param rotation target rigidbody rotation
	 * @param activate is rigidbody should be activated
	 */
	void setRotation(quat rotation, bool activate = true);

	/**
	 * @brief Returns rigidbody position and rotation in the physics simulation world.
	 * 
	 * @param[out] position rigidbody position
	 * @param[out] rotation rigidbody rotation
	 */
	void getPosAndRot(f32x4& position, quat& rotation) const;
	/**
	 * @brief Sets rigidbody position and rotation in the physics simulation world.
	 *
	 * @param position target rigidbody position
	 * @param rotation target rigidbody rotation
	 * @param activate is rigidbody should be activated
	 * @param leaveLast leave last position and rotation intact
	 */
	void setPosAndRot(f32x4 position, quat rotation, bool activate = true, bool leaveLast = false);
	/**
	 * @brief Are rigidbody position and rotation differ from specified values.
	 * @note It also checks if values are far enough to count it as changed.
	 *
	 * @param position target rigidbody position
	 * @param rotation target rigidbody rotation
	 */
	bool isPosAndRotChanged(f32x4 position, quat rotation) const;

	/*******************************************************************************************************************
	 * @brief Returns world space linear velocity of the center of mass. (m/s)
	 */
	f32x4 getLinearVelocity() const;
	/**
	 * @brief Sets world space linear velocity of the center of mass. (m/s)
	 * @param velocity target linear velocity
	 */
	void setLinearVelocity(f32x4 velocity);

	/**
	 * @brief Returns world space angular velocity of the center of mass. (rad/s)
	 */
	f32x4 getAngularVelocity() const;
	/**
	 * @brief Sets world space angular velocity of the center of mass. (rad/s)
	 * @param velocity target angular velocity
	 */
	void setAngularVelocity(f32x4 velocity);

	/**
	 * @brief Returns velocity of point (in world space, e.g. on the surface of the body) of the body. (m/s)
	 * @param point target velocity point
	 */
	f32x4 getPointVelocity(f32x4 point) const;
	/**
	 * @brief Returns velocity of point (in center of mass space, e.g. on the surface of the body) of the body. (m/s)
	 * @param point target velocity point
	 */
	f32x4 getPointVelocityCOM(f32x4 point) const;

	/**
	 * @brief Set velocity of rigidbody such that it will be positioned at position/rotation in deltaTime.
	 * @note It will activate rigidbody if needed.
	 *
	 * @param position target rigidbody position
	 * @param rotation target rigidbody rotation
	 * @param deltaTime target delta time in seconds
	 */
	void moveKinematic(f32x4 position, quat rotation, float deltaTime);

	/**
	 * @brief Sets rigidbody world space position and rotation from a transform.
	 * @param activate is rigidbody should be activated
	 */
	void setWorldTransform(bool activate = true);

	/*******************************************************************************************************************
	 * @brief Creates a new constraint between two rigidbodies.
	 *
	 * @param otherBody target rigidbody to attach to or null (null = world)
	 * @param type bodies connection type
	 * @param thisBodyPoint bodies connection type
	 * @param otherBodyPoint bodies connection type
	 */
	void createConstraint(ID<Entity> otherBody, ConstraintType type, 
		f32x4 thisBodyPoint = f32x4::max, f32x4 otherBodyPoint = f32x4::max);
	/**
	 * @brief Destroys constraint between two rigidbodies.
	 * @param index target constraint index in the array
	 */
	void destroyConstraint(uint32 index);
	/**
	 * @brief Destroys all constraints between this rigidbody.
	 */
	void destroyAllConstraints();

	/**
	 * @brief Is constraint enabled and used in the simulation.
	 * @param index target constraint index in the array
	 */
	bool isConstraintEnabled(uint32 index) const;
	/**
	 * @brief Sets constraint enabled state.
	 * @param index target constraint index in the array
	 */
	void setConstraintEnabled(uint32 index, bool isEnabled);

	/**
	 * @brief Retruns rigidbody internal instance.
	 * @warning Use only if you know what you are doing!
	 */
	void* getInstance() const noexcept { return instance; }

	// TODO: implement notifiers of shape changes, we should do it manually for constraints
};

/***********************************************************************************************************************
 * @brief Provides an approximate simulation of rigid body dynamics (including collision detection).
 * @details Registers events: Simulate.
 */
class PhysicsSystem final : public ComponentSystem<RigidbodyComponent, false>, 
	public Singleton<PhysicsSystem>, public ISerializable, public INetworkable
{
public:
	using ShapePool = LinearPool<Shape>;
	using SharedShapes = tsl::robin_map<Hash128, ID<Shape>>;

	/**
	 * @brief Physics simulation system properties.
	 */
	struct Properties final
	{
		uint32 tempBufferSize = 10 * 1024 * 1024; // 10mb
		uint32 maxRigidbodyCount = 65536;
		uint32 bodyMutexCount = 0; // 0 = auto
		uint32 maxBodyPairCount = 65536;
		uint32 maxContactConstraintCount = 10240;
		uint16 collisionLayerCount = (uint16)CollisionLayer::DefaultCount;
		uint16 broadPhaseLayerCount = (uint16)BroadPhaseLayer::DefaultCount;
		Properties() noexcept { }
	};

	/**
	 * @brief Physics simulation event container
	 */
	struct Event final
	{
		uint32 data1 = 0;
		uint32 data2 = 0;
		BodyEvent eventType = {};
	private:
		uint8 _alignment0 = 0;
		uint16 _alignment1 = 0;
	};

	struct NetRigidbody final
	{
		quat rotation;
		float3 position;
		float3 linearVelocity;
		float3 angularVelocity;
	};
	static constexpr const char* messageType = "r";
private:
	struct EntityConstraint final
	{
		uint64 otherUID = 0;
		ID<Entity> entity = {};
		ConstraintType type = {};
	};

	Properties properties;
	ShapePool shapes;
	SharedShapes sharedEmptyShapes;
	SharedShapes sharedBoxShapes;
	SharedShapes sharedSphereShapes;
	SharedShapes sharedCapsuleShapes;
	SharedShapes sharedRotTransShapes;
	SharedShapes sharedCustomShapes;
	vector<Event> bodyEvents;
	stack<ID<Entity>, vector<ID<Entity>>> entityStack;
	set<ID<Entity>> serializedConstraints;
	tsl::robin_map<uint64, ID<Entity>> deserializedEntities;
	vector<EntityConstraint> deserializedConstraints;
	tsl::robin_map<uint32, NetRigidbody> netRigidbodies;
	mutex bodyEventLocker, netRigidbodyLocker;
	string valueStringCache;
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
	const void* narrowPhaseQuery = nullptr;
	const void* broadPhaseQuery = nullptr;
	ID<Entity> thisBody = {}, otherBody = {};
	float deltaTimeAccum = 0.0f;
	uint32 cascadeLagCount = 0;

	#if GARDEN_DEBUG
	set<uint64> serializedEntities;
	#endif

	/**
	 * @brief Creates a new physics system instance.
	 * 
	 * @param properties target physics simulation properties
	 * @param setSingleton set system singleton instance
	 */
	PhysicsSystem(const Properties& properties = {}, bool setSingleton = true);
	/**
	 * @brief Destroys physics system instance.
	 */
	~PhysicsSystem() final;

	void preInit();
	void postInit();
	void simulate();

	void prepareSimulate();
	void processSimulate();
	void interpolateResult(float t);
	void flushNetRigidbodies();
	void sendServerMessages();

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;
	void disposeComponents() final;
	
	void serializeDecoratedShape(ISerializer& serializer, ID<Shape> shape);
	void serialize(ISerializer& serializer, const View<Component> component) final;
	void postSerialize(ISerializer& serializer) final;
	ID<Shape> deserializeDecoratedShape(IDeserializer& deserializer, string& valueStringCache);
	void deserialize(IDeserializer& deserializer, View<Component> component) final;
	void postDeserialize(IDeserializer& deserializer) final;

	string_view getMessageType() final;
	int onMsgFromClient(ClientSession* session, StreamInput message) final;
	int onMsgFromServer(StreamInput message, bool isDatagram) final;
	
	friend class ecsm::Manager;
	friend struct RigidbodyComponent;
	friend struct CharacterComponent;
	friend class CharacterSystem;
public:
	/******************************************************************************************************************/
	float cascadeLagThreshold = 0.1f;  /**< Underperforming simulation frames threshold to try recover performance. */
	float networkViewRadius = 1000.0f; /**< Network world bodies synchronization radius. */
	int32 collisionSteps = 1;          /**< Collision step count during simulation step. */
	uint16 simulationRate = 60;        /**< Simulation update count per second. */
	

	#if GARDEN_DEBUG || GARDEN_EDITOR
	float statsLogRate = 10.0f;       /**< Simulation debug stats log rate in seconds. */
	bool logBroadPhaseStats = false;  /**< Log simulation broad phase debug stats. */
	bool logNarrowPhaseStats = false; /**< Log simulation narrow phase debug stats. */
	#endif

	/**
	 * @brief Returns physics system creation properties
	 */
	const Properties& getProperties() const noexcept { return properties; }
	/**
	 * @brief Returns physics shape pool.
	 */
	const ShapePool& getShapes() const noexcept { return shapes; }
	/**
	 * @brief Returns shared empty shape pool.
	 */
	const SharedShapes& getSharedEmptyShapes() const noexcept { return sharedEmptyShapes; }
	/**
	 * @brief Returns shared box shape pool.
	 */
	const SharedShapes& getSharedBoxShapes() const noexcept { return sharedBoxShapes; }
	/**
	 * @brief Returns shared sphere shape pool.
	 */
	const SharedShapes& getSharedSphereShapes() const noexcept { return sharedSphereShapes; }
	/**
	 * @brief Returns shared capsule shape pool.
	 */
	const SharedShapes& getSharedCapsuleShapes() const noexcept { return sharedCapsuleShapes; }
	/**
	 * @brief Returns shared rotated/translated shape pool.
	 */
	const SharedShapes& getSharedRotTransShapes() const noexcept { return sharedRotTransShapes; }
	/**
	 * @brief Returns shared custom shape pool.
	 */
	const SharedShapes& getSharedCustomShapes() const noexcept { return sharedCustomShapes; }

	/**
	 * @brief Returns current this rigidbody entity.
	 * @details Useful inside rigidbody event. (see eventListener)
	 */
	ID<Entity> getThisBody() const noexcept { return thisBody; }
	/**
	 * @brief Returns current other rigidbody entity. (may be null)
	 * @details Useful inside rigidbody event. (see eventListener)
	 */
	ID<Entity> getOtherBody() const noexcept { return otherBody; }

	/**
	 * @brief Returns global gravity vector.
	 */
	f32x4 getGravity() const noexcept;
	/**
	 * @brief Sets global gravity vector.
	 * @param gravity target gravity vector
	 */
	void setGravity(f32x4 gravity) const noexcept;

	/*******************************************************************************************************************
	 * @brief Creates a new empty shape instance.
	 */
	ID<Shape> createEmptyShape(f32x4 centerOfMass = f32x4::zero);
	/**
	 * @brief Creates a new shared box shape instance.
	 * @details See the @ref createEmptyShape().
	 */
	ID<Shape> createSharedEmptyShape(f32x4 centerOfMass = f32x4::zero);

	/**
	 * @brief Creates a new box shape instance.
	 * 
	 * @details
	 * Internally the convex radius will be subtracted from the half 
	 * extent so the total box will not grow with the convex radius.
	 * 
	 * @param halfExtent half box edge length
	 * @param convexRadius box convex radius
	 * @param density box density (kg / m^3)
	 */
	ID<Shape> createBoxShape(f32x4 halfExtent, float convexRadius = 0.05f, float density = 1000.0f);
	/**
	 * @brief Creates a new shared box shape instance.
	 * @details See the @ref createBoxShape().
	 *
	 * @param halfExtent half box edge length
	 * @param convexRadius box convex radius
	 * @param density box density (kg / m^3)
	 */
	ID<Shape> createSharedBoxShape(f32x4 halfExtent, float convexRadius = 0.05f, float density = 1000.0f);

	/**
	 * @brief Creates a new sphere shape instance.
	 * 
	 * @param radius sphere radius
	 * @param density sphere density (kg / m^3)
	 */
	ID<Shape> createSphereShape(float radius, float density = 1000.0f);
	/**
	 * @brief Creates a new shared sphere shape instance.
	 * @details See the @ref createSphereShape().
	 *
	 * @param radius sphere radius
	 * @param density sphere density (kg / m^3)
	 */
	ID<Shape> createSharedSphereShape(float radius, float density = 1000.0f);

	/**
	 * @brief Creates a new capsule shape instance.
	 * 
	 * @param halfheight capsule cylinder half height
	 * @param radius capsule half sphere cap radius
	 * @param density capsule density (kg / m^3)
	 */
	ID<Shape> createCapsuleShape(float halfheight, float radius, float density = 1000.0f);
	/**
	 * @brief Creates a new shared capsule shape instance.
	 * @details See the @ref createCapsuleShape().
	 *
	 * @param halfheight capsule cylinder half height
	 * @param radius capsule half sphere cap radius
	 * @param density capsule density (kg / m^3)
	 */
	ID<Shape> createSharedCapsuleShape(float halfheight, float radius, float density = 1000.0f);

	/**
	 * @brief Creates a new rotated/translated shape instance.
	 * @details Rotates and translates an inner (child) shape.
	 * @note It destroys inner shape if no more refs left.
	 *
	 * @param innerShape target inner shape instance
	 * @param position shape translation
	 * @param rotation shape rotation
	 */
	ID<Shape> createRotTransShape(ID<Shape> innerShape, f32x4 position, quat rotation = quat::identity);
	/**
	 * @brief Creates a new shared rotated/translated shape instance.
	 * @details See the @ref createRotTransShape().
	 * @note It destroys inner shape if no more refs left.
	 *
	 * @param innerShape target inner shape instance
	 * @param halfExtent half edge length
	 * @param position shape translation
	 * @param rotation shape rotation
	 */
	ID<Shape> createSharedRotTransShape(ID<Shape> innerShape, f32x4 position, quat rotation = quat::identity);

	/**
	 * @brief Creates a new custom shape instance.
	 * @param[in] shapeInstance target custom shape instance
	 */
	ID<Shape> createCustomShape(void* shapeInstance);
	/**
	 * @brief Creates a new shared custom shape instance.
	 * @details See the @ref createCustomShape().
	 * @param[in] shapeInstance target custom shape instance
	 */
	ID<Shape> createSharedCustomShape(void* shapeInstance);

	/*******************************************************************************************************************
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

	/*******************************************************************************************************************
	 * @brief Improves collision detection performance. (Expensive operation!)
	 */
	void optimizeBroadPhase();

	/**
	 * @brief Casts a ray to find the closest rigidbody shape hit.
	 * 
	 * @param[in] ray target ray to cast
	 * @param[out] hit rigidbody shape hit information
	 * @param maxDistance maximum ray cast distance
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 * @param castInactive also cast inactive rigidbodies
	 */
	RayCastHit castRay(const Ray& ray, float maxDistance = 1000.0f, 
		int8 broadPhaseLayer = -1, bool castInactive = false);
	/**
	 * @brief Casts a ray to find all rigidbody shape hits.
	 * 
	 * @param[in] ray target ray to cast
	 * @param[out] hits rigidbody shape hit information array
	 * @param maxDistance maximum ray cast distance
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 * @param sortHits order hits on closest first
	 * @param castInactive also cast inactive rigidbodies
	 */
	void castRay(const Ray& ray, vector<RayCastHit>& hits, float maxDistance = 1000.0f, 
		int8 broadPhaseLayer = -1, bool sortHits = false, bool castInactive = false);

	/**
	 * @brief Collides specified AABB with all rigidbody AABBs. (Broad phase)
	 * 
	 * @param aabb target axis aligned bounding box to collide
	 * @param[out] hits rigidbody shape hit information array
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 */
	void collideAABB(const Aabb& aabb, vector<ShapeHit>& hits, int8 broadPhaseLayer = -1);
	/**
	 * @brief Collides specified sphere with all rigidbody AABBs. (Broad phase)
	 * 
	 * @param sphere target sphere to collide
	 * @param[out] hits rigidbody shape hit information array
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 */
	void collideSphere(Sphere sphere, vector<ShapeHit>& hits, int8 broadPhaseLayer = -1);

	/**
	 * @brief Collides specified point with a first rigidbody shape.
	 * 
	 * @param point target point to collide
	 * @param[out] hit rigidbody shape hit information
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 * @param collideInactive also collide inactive rigidbodies
	 */	
	ShapeHit collidePoint(f32x4 point, int8 broadPhaseLayer = -1, bool collideInactive = false);
	/**
	 * @brief Collides specified point with all rigidbody shape.
	 * 
	 * @param point target point to collide
	 * @param[out] hits rigidbody shape hit information array
	 * @param broadPhaseLayer broad phase layer filter (-1 = all)
	 * @param collideInactive also collide inactive rigidbodies
	 */	
	void collidePoint(f32x4 point, vector<ShapeHit>& hits, int8 broadPhaseLayer = -1, bool collideInactive = false);

	/*******************************************************************************************************************
	 * @brief Wakes up rigidbody if it's sleeping.
	 * @details It also wakes up all rigidbody descendants.
	 */
	void activateRecursive(ID<Entity> entity);
	/**
	 * @brief Sets rigidbody world space position and rotation from a transform.
	 * @details It also sets world transform for all rigidbody descendants.
	 *
	 * @param entity target rigidbody entity instance
	 * @param activate are rigidbodies should be activated
	 */
	void setWorldTransformRecursive(ID<Entity> entity, bool activate = true);

	/**
	 * @brief Enabled collision between two rigidbody layers.
	 */
	void enableCollision(uint16 collisionLayer1, uint16 collisionLayer2);
	/**
	 * @brief Disables collision between two rigidbody layers.
	 */
	void disableCollision(uint16 collisionLayer1, uint16 collisionLayer2);
	/**
	 * @brief Is collision enabled between the specified rigidbody layers.
	 */
	bool isCollisionEnabled(uint16 collisionLayer1, uint16 collisionLayer2) const;

	/**
	 * @brief Maps rigidbody collision and broad phase layers.
	 */
	void mapLayers(uint16 collisionLayer, uint8 broadPhaseLayer);

	#if GARDEN_DEBUG
	/**
	 * @brief Sets rigidbody broad phase layer debug name. (Debug only)
	 */
	void setBroadPhaseLayerName(uint8 broadPhaseLayer, const string& debugName);
	#endif

	/**
	 * @brief Returns physics system internal instance.
	 * @warning Use only if you know what you are doing!
	 */
	void* getInstance() const noexcept { return physicsInstance; }
};

} // namespace garden
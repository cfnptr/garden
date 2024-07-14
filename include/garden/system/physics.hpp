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

#pragma once
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

/**
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


/**
 * @brief Collision volume shape type
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

/**
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
	ShapeType getType() const;
	ShapeSubType getSubType() const;

	uint64 getRefCount() const;
	bool isLastRef() const;
};

} // garden::physics

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace physics;

struct RigidbodyComponent final : public Component
{
private:
	uint32 instance = 0;
	ID<Shape> shape = {};
	MotionType motionType = {};

	bool destroy();

	friend class PhysicsSystem;
	friend class LinearPool<RigidbodyComponent>;
public:
	MotionType getMotionType() const noexcept { return motionType; }
	void setMotionType(MotionType motionType, bool activate = true);

	ID<Shape> getShape() const noexcept { return shape; }
	void setShape(ID<Shape> shape, bool activate = true);

	bool isActive() const;
	void activate();
	void deactivate();

	float3 getPosition() const;
	void setPosition(const float3& position, bool activate = true);
	quat getRotation() const;
	void setRotation(const quat& rotation, bool activate = true);
	void getPosAndRot(float3& position, quat& rotation) const;
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
	void* tempAllocator = nullptr;
	void* jobSystem = nullptr;
	void* bpLayerInterface = nullptr;
	void* objVsBpLayerFilter = nullptr;
	void* objVsObjLayerFilter = nullptr;
	void* physicsInstance = nullptr;
	void* bodyInterface = nullptr;
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
	void update();

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
	int32 collisionSteps = 1;
	uint16 simulationRate = 60;

	/**
	 * @brief Returns physics component pool.
	 */
	const LinearPool<RigidbodyComponent>& getComponents() const noexcept { return components; }
	/**
	 * @brief Returns physics shape pool.
	 */
	const LinearPool<Shape>& getShapes() const noexcept { return shapes; }

	ID<Shape> createBoxShape(const float3& halfExtent, float convexRadius = 0.05f);

	View<Shape> get(ID<Shape> shape) const noexcept { return shapes.get(shape); }
	void destroy(ID<Shape> shape) { shapes.destroy(shape); }

	/**
	 * @brief Improves collision detection performance. (Expensive operation!)
	 */
	void optimizeBroadPhase();

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
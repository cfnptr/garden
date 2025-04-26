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

/***********************************************************************************************************************
 * @file
 * @brief Character physics functions.
 * @details See the Jolt physics docs: https://jrouwe.github.io/JoltPhysics/index.html
 */

#pragma once
#include "garden/system/physics.hpp"
#include "math/angles.hpp"

namespace garden
{

using namespace garden::physics;
class CharacterSystem;

/**
 * @brief State of the character ground, is he standing on ground or midair, or on steep ground.
 */
enum class CharacterGround : uint8
{
	OnGround, /**< Character is on the ground and can move freely. */
	/**< Character is on a slope that is too steep and can't climb up any further. 
	     The caller should start applying downward velocity if sliding from the slope is desired. */
	OnSteepGround,
	/**< Character is touching an object, but is not supported by it and should fall. 
	     The getGroundXXX functions will return information about the touched object. */
	NotSupported,
	InAir, /**< Character is in the air and is not touching anything. */
	Count, /**< Character ground state count. */
};

/***********************************************************************************************************************
 * @brief Physics character controller.
 */
struct CharacterComponent final : public Component
{
public:
	/**
	 * @brief Character update settings container.
	 */
	struct UpdateSettings final
	{
		f32x4 stepDown = f32x4(0.0f, -0.5f, 0.0f);       /**< Max amount to project the character downwards on stick to floor. */
		f32x4 stepUp = f32x4(0.0f, 0.4f, 0.0f);          /**< Direction and distance to step up. (zero = disabled) */
		f32x4 stepDownExtra = f32x4::zero;               /**< Additional translation added when stepping down at the end. */
		float minStepForward = 0.02f;                    /**< Magnitude to step forward after the step up. */
		float stepForwardTest = 0.15f;                   /**< Additional step test magnitude when running at a high frequency. */
		float forwardContact = std::cos(degrees(75.0f)); /**< Maximum angle between ground normal and the character forward vector. */
	};
private:
	ID<Shape> shape = {};
	void* instance = nullptr;
	bool inSimulation = true;
	uint8 _alignment0 = 0;

	bool destroy();

	friend class CharacterSystem;
	friend class LinearPool<CharacterComponent>;
	friend class ComponentSystem<CharacterComponent>;
public:
	uint16 collisionLayer = (uint16)CollisionLayer::Moving; /**< Character collision layer index. */

	/**
	 * @brief Returns character shape instance.
	 */
	ID<Shape> getShape() const noexcept { return shape; }
	/**
	 * @brief Sets character shape instance. (Expensive operation!)
	 *
	 * @details
	 * It also creates character instance if it doesn't already exists,
	 * and adds it to the physics simulation if transform is active.
	 * 
	 * @note Make sure the shape is made so that the bottom of the shape is at (0, 0, 0)!
	 *
	 * @param shape target shape instance or null
	 * @param mass character mass (kg). Used to push down objects with gravity when the character is standing on top
	 * @param maxPenetrationDepth max penetration we're willing to accept after the switch (if not FLT_MAX)
	 */
	void setShape(ID<Shape> shape, float mass = 70.0f, float maxPenetrationDepth = FLT_MAX);

	/**
	 * @brief Returns character position in the physics simulation world.
	 */
	f32x4 getPosition() const;
	/**
	 * @brief Sets character position in the physics simulation world.
	 * @param position target character position
	 */
	void setPosition(f32x4 position);

	/**
	 * @brief Returns character rotation in the physics simulation world.
	 */
	quat getRotation() const;
	/**
	 * @brief Sets character rotation in the physics simulation world.
	 * @param rotation target character rotation
	 */
	void setRotation(quat rotation);

	/**
	 * @brief Returns character position and rotation in the physics simulation world.
	 *
	 * @param[out] position character position
	 * @param[out] rotation character rotation
	 */
	void getPosAndRot(f32x4& position, quat& rotation) const;
	/**
	 * @brief Sets character position and rotation in the physics simulation world.
	 *
	 * @param position target character position
	 * @param rotation target character rotation
	 */
	void setPosAndRot(f32x4 position, quat rotation);
	/**
	 * @brief Are character position and rotation differ from the specified values.
	 * @note It also checks if values are far enough to count it as changed.
	 *
	 * @param position target character position
	 * @param rotation target character rotation
	 */
	bool isPosAndRotChanged(f32x4 position, quat rotation) const;

	/*******************************************************************************************************************
	 * @brief Returns character linear velocity. (m/s)
	 */
	f32x4 getLinearVelocity() const;
	/**
	 * @brief Sets character linear velocity. (m/s)
	 * @param velocity target linear velocity
	 */
	void setLinearVelocity(f32x4 velocity);

	/**
	 * @brief Returns character ground velocity. (m/s)
	 */
	f32x4 getGroundVelocity() const;
	/**
	 * @brief Returns current character ground state.
	 */
	CharacterGround getGroundState() const;

	/**
	 * @brief Returns character mass. (kg)
	 */
	float getMass() const;
	/**
	 * @brief Sets character mass. (kg)
	 * @param mass target character mass
	 */
	void setMass(float mass);
	
	/*******************************************************************************************************************
	 * @brief Moves the character according to its current velocity.
	 * 
	 * @details
	 * The character is similar to a kinematic rigidbody in the sense that you set the velocity and the character 
	 * will follow unless collision is blocking the way. Note it's your own responsibility to apply gravity to the 
	 * character velocity! Different surface materials (like ice) can be emulated by getting the ground material and 
	 * adjusting the velocity and/or the max slope angle accordingly every frame.
	 * 
	 * Before calling, call SetLinearVelocity to update the horizontal/vertical speed of the character, typically this is:
	 *  - When on OnGround and not moving away from ground: velocity = GetGroundVelocity() + horizontal speed as 
	 *    input by player + optional vertical jump velocity + delta time * gravity
	 *  - Else: velocity = current vertical velocity + horizontal speed as input by player + delta time * gravity
	 *
	 * @param deltaTime time step to simulate
	 * @param gravity vector (m/s^2). Only used when the character is standing on top of another object to apply downward force.
	 * @param[in] settings extended character update settings
	 */
	void update(float deltaTime, f32x4 gravity, const UpdateSettings* settings = nullptr);

	/**
	 * @brief Returns true if the character has moved into a slope that is too steep (e.g. a vertical wall).
	 * @details You would call WalkStairs to attempt to step up stairs.
	 * @param linearVelocity linear velocity used to determine if we're pushing into a step
	 */
	bool canWalkStairs(f32x4 linearVelocity) const;
	/**
	 * @brief Cast up, forward and down again to try to find a valid position for stair walking.
	 * 
	 * @param deltaTime time step to simulate
	 * @param stepUp direction and distance to step up (this corresponds to the max step height)
	 * @param stepForward direction and distance to step forward after the step up 
	 * @param stepForwardTest additional step test when running at a high frequency (prevents normal that violates the max slope angle)
	 * @param stepDownExtra additional translation that is added when stepping down at the end (zero = disabled)
	 * 
	 * @return If stair walk was successful.
	 */
	bool walkStairs(float deltaTime, f32x4 stepUp, f32x4 stepForward, f32x4 stepForwardTest, f32x4 stepDownExtra);

	/**
	 * @brief Can be used to artificially keep the character to the floor.
	 * 
	 * @details
	 * Normally when a character is on a small step and starts moving horizontally, the character will lose contact with 
	 * the floor because the initial vertical velocity is zero while the horizontal velocity is quite high. To prevent 
	 * the character from losing contact with the floor, we do an additional collision check downwards and if we find 
	 * the floor within a certain distance, we project the character onto the floor.
	 * 
	 * @param stepDown max amount to project the character downwards
	 * @return True if character was successfully projected onto the floor.
	 */
	bool stickToFloor(f32x4 stepDown);

	/**
	 * @brief Sets character world space position and rotation from a transform.
	 */
	void setWorldTransform();
};

/***********************************************************************************************************************
 * @brief Provides an simulation of physics character controllers.
 * 
 * @details
 * Can be used to create a character controller. These are usually used to represent the player as a simple 
 * capsule or tall box and perform collision detection while the character navigates through the world.
 * 
 * Character is implemented using collision detection functionality only (through NarrowPhaseQuery) and is simulated 
 * when update() is called. Since the character is not 'added' to the world, it is not visible to rigid bodies and it 
 * only interacts with them during the update() function by applying impulses. This does mean there can be some update 
 * order artifacts, like the character slightly hovering above an elevator going down, because the characters moves at 
 * a different time than the other rigid bodies. Separating it has the benefit that the update can happen at the 
 * appropriate moment in the game code.
 * 
 * If you want character to have presence in the world, it is recommended to pair it with a slightly smaller Kinematic 
 * body. After each update, move this body using moveKinematic to the new location. This ensures that standard collision 
 * tests like ray casts are able to find the character in the world and that fast moving objects with motion quality 
 * LinearCast will not pass through the character in 1 update. As an alternative to a Kinematic body, you can also use 
 * a regular Dynamic body with a gravity factor of 0. Ensure that the character only collides with dynamic objects in 
 * this case. The advantage of this approach is that the paired body doesn't have infinite mass so is less strong.
 * 
 * Character has the following extra functionality:
 *   Sliding along walls
 *   Interaction with elevators and moving platforms
 *   Enhanced steep slope detection (standing in a funnel whose sides 
 *     are too steep to stand on will not be considered as too steep)
 *   Stair stepping through the extendedUpdate() call
 *   Sticking to the ground when walking down a slope through the extendedUpdate() call
 *   Support for specifying a local coordinate system that allows e.g. walking around in a flying space ship that is 
 *     equipped with 'inertial dampers' (a sci-fi concept often used in games).
 */
class CharacterSystem final : public ComponentSystem<CharacterComponent>, 
	public Singleton<CharacterSystem>, public ISerializable
{
	vector<ID<Entity>> entityStack;
	void* charVsCharCollision = nullptr;
	string valueStringCache;
public:
	/**
	 * @brief Creates a new character system instance.
	 * @param setSingleton set system singleton instance
	 */
	CharacterSystem(bool setSingleton = true);
	/**
	 * @brief Destroys character system instance.
	 */
	~CharacterSystem() final;

	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, View<Component> component) final;

	friend class ecsm::Manager;
	friend struct CharacterComponent;
public:
	/**
	 * @brief Sets character world space position and rotation from a transform.
	 * @details It also sets world transform for all character descendants.
	 *
	 * @param entity target character entity instance
	 */
	void setWorldTransformRecursive(ID<Entity> entity);
};

// TODO: support non virtual Jolt JPH::Character for AI characters/players.

} // namespace garden
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

/***********************************************************************************************************************
 * @file
 * @brief Character physics functions.
 * @details See the Jolt physics docs: https://jrouwe.github.io/JoltPhysics/index.html
 */

#pragma once
#include "garden/system/physics.hpp"

namespace garden
{

using namespace math;
using namespace ecsm;
class CharacterSystem;

/***********************************************************************************************************************
 * @brief State of the character ground, is he standing on ground or midair, or on steep ground.
 */
enum class CharacterGround : uint8
{
	/**< Character is on the ground and can move freely. */
	OnGround,
	/**< Character is on a slope that is too steep and can't climb up any further. 
	     The caller should start applying downward velocity if sliding from the slope is desired. */
	OnSteepGround,
	/**< Character is touching an object, but is not supported by it and should fall. 
	     The getGroundXXX functions will return information about the touched object. */
	NotSupported,
	/**< Character is in the air and is not touching anything. */
	InAir,
	/**< Character ground ctate count. */
	Count,
};

/**
 * @brief Physics character controller.
 */
struct CharacterComponent final : public Component
{
private:
	ID<Shape> shape = {};
	void* instance = nullptr;
	bool inSimulation = true;
	uint8 _alignment0 = 0;

	bool destroy();

	friend class CharacterSystem;
	friend class LinearPool<CharacterComponent>;
public:
	/**
	 * @brief Character collision layer index.
	 */
	uint16 collisionLayer = (uint16)CollisionLayer::Moving;

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
	 * @param shape target shape instance or null
	 * @param mass character mass (kg). Used to push down objects with gravity when the character is standing on top.
	 * @param maxPenetrationDepth max penetration we're willing to accept after the switch (if not FLT_MAX).
	 */
	void setShape(ID<Shape> shape, float mass = 70.0f, float maxPenetrationDepth = FLT_MAX);

	/**
	 * @brief Returns character position in the phyics simulation world.
	 */
	float3 getPosition() const;
	/**
	 * @brief Sets character position in the phyics simulation world.
	 * @param[in] position target character position
	 */
	void setPosition(const float3& position);

	/**
	 * @brief Returns character rotation in the phyics simulation world.
	 */
	quat getRotation() const;
	/**
	 * @brief Sets character rotation in the phyics simulation world.
	 * @param[in] rotation target character rotation
	 */
	void setRotation(const quat& rotation);

	/**
	 * @brief Returns character position and rotation in the phyics simulation world.
	 *
	 * @param[out] position character position
	 * @param[out] rotation character rotation
	 */
	void getPosAndRot(float3& position, quat& rotation) const;
	/**
	 * @brief Sets character position and rotation in the phyics simulation world.
	 *
	 * @param[in] position target character position
	 * @param[in] rotation target character rotation
	 */
	void setPosAndRot(const float3& position, const quat& rotation);
	/**
	 * @brief Returns true if character position and rotation differs from specified values.
	 * @note It also checks if values are far enought to count it as changed.
	 *
	 * @param[in] position target character position
	 * @param[in] rotation target character rotation
	 */
	bool isPosAndRotChanged(const float3& position, const quat& rotation) const;

	/**
	 * @brief Returns character linear velocity. (m/s)
	 */
	float3 getLinearVelocity() const;
	/**
	 * @brief Sets character linear velocity. (m/s)
	 * @param[in] velocity target linear velocity
	 */
	void setLinearVelocity(const float3& velocity);

	/**
	 * @brief Returns current character ground state.
	 */
	CharacterGround getGroundState() const;

	/**
	 * @brief Returns characater mass. (kg)
	 */
	float getMass() const;
	/**
	 * @brief Sets characater mass. (kg)
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
	 * @param deltaTime time step to simulate
	 * @param[in] gravity vector (m/s^2). Only used when the character is standing on top of another object to apply downward force.
	 */
	void update(float deltaTime, const float3& gravity);
	/**
	 * @brief This function combines Update, StickToFloor and WalkStairs.
	 *
	 * @details
	 * This function serves as an example of how these functions could be combined.
	 * 
	 * Before calling, call SetLinearVelocity to update the horizontal/vertical speed of the character, typically this is:
	 *  - When on OnGround and not moving away from ground: velocity = GetGroundVelocity() + horizontal speed as 
	 *    input by player + optional vertical jump velocity + delta time * gravity
	 *  - Else: velocity = current vertical velocity + horizontal speed as input by player + delta time * gravity
	 *
	 * @param deltaTime time step to simulate
	 * @param[in] gravity vector (m/s^2). Only used when the character is standing on top of another object to apply downward force.
	 */
	void extendedUpdate();

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
class CharacterSystem final : public System, public ISerializable
{
	LinearPool<CharacterComponent> components;
	vector<ID<Entity>> entityStack;
	void* charVsCharCollision = nullptr;
	string valueStringCache;

	static CharacterSystem* instance;
public:
	/**
	 * @brief Creates a new character system instance.
	 */
	CharacterSystem();
	/**
	 * @brief Destroy character system instance.
	 */
	~CharacterSystem() final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;

	friend class ecsm::Manager;
	friend class CharacterComponent;
public:
	/**
	 * @brief Sets character world space position and rotation from a transform.
	 * @details It also sets world transform for all character descendants.
	 *
	 * @param entity target character enity instance
	 */
	void setWorldTransformRecursive(ID<Entity> entity);

	/**
	 * @brief Returns true if entity has character component.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	bool has(ID<Entity> entity) const;
	/**
	 * @brief Returns entity character component view.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<CharacterComponent> get(ID<Entity> entity) const;
	/**
	 * @brief Returns entity character component view if exist.
	 * @param entity target entity with component
	 * @note This function is faster than the Manager one.
	 */
	View<CharacterComponent> tryGet(ID<Entity> entity) const;

	/**
	 * @brief Returns character system instance.
	 */
	static CharacterSystem* get() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

// TODO: support non virtual Jolt JPH::Character for AI characters/players.

} // namespace garden
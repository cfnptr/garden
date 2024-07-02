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
 * @brief Common camera projection functions.
 */

#pragma once
#include "garden/animate.hpp"
#include "math/angles.hpp"

namespace garden
{

/**
 * @brief Generic head-mounted display depth.
 */
const float defaultHmdDepth = 0.01f;
/**
 * @brief Optimal FOV for a PC monitor.
 */
const float defaultFieldOfView = 90.0f;
/**
 * @brief Generic 16/9 display aspect ratio.
 */
const float defaultAspectRatio = (16.0f / 9.0f);

using namespace math;
using namespace ecsm;

/**
 * @brief Camera projection type.
 */
enum class ProjectionType : uint8
{
	Perspective, Orthographic, Count,
};

/**
 * @brief Perspective camera projection properties.
 * 
 * @details
 * Method used to simulate the way the human eye perceives the world, creating a sense of depth in a scene. This 
 * projection technique helps in rendering a three-dimensional scene onto a two-dimensional display by mimicking 
 * the way objects appear to the eye, with objects appearing smaller as they are further away from the viewer.
 */
struct PerspectiveProjection final
{
	float fieldOfView = radians(defaultFieldOfView);
	float aspectRatio = defaultAspectRatio;
	float nearPlane = defaultHmdDepth;
private:
	float _alignment0 = 0.0f;
	float2 _alignment1 = float2(0.0f);
};
/**
 * @brief Orthographic camera projection properties.
 * 
 * @details
 * Method used to render three-dimensional objects in two dimensions without the depth distortion that comes with 
 * perspective projection. In orthographic projection, the size of objects remains constant regardless of their 
 * distance from the camera, which means there is no perspective foreshortening or vanishing points.
 */
struct OrthographicProjection final
{
	float2 width = float2(-1.0f, 1.0f);
	float2 height = float2(-1.0f, 1.0f);
	float2 depth = float2(-1.0f, 1.0f);
};
	
/**
 * @brief Camera perspective/orthographic projection properties.
 * @details See the @ref CameraComponent::Perspective and @ref CameraComponent::Orthographic.
 */
union CameraProjection final
{
	PerspectiveProjection perspective;   /**< Perspective camera projection properties. */
	OrthographicProjection orthographic; /**< Orthographic camera projection properties. */
	CameraProjection() : perspective() { }
};

/***********************************************************************************************************************
 * @brief Contains information about camera projection properties.
 */
struct CameraComponent final : public Component
{
	CameraProjection p = {};                           /**< Camera perspective/orthographic projection properties. */
	ProjectionType type = ProjectionType::Perspective; /**< Camera projection type. */
private:
	uint8_t _alignment0 = 0;
	uint16_t _alignment1 = 0;

	friend class CameraSystem;
	friend class LinearPool<CameraComponent, false>;
public:
	/**
	 * @brief Calculates camera projection matrix.
	 * @details See the @ref calcPerspProjInfRevZ().
	 */
	float4x4 calcProjection() const noexcept;
	/**
	 * @brief Returns camera projection matrix near plane.
	 * @details Handles projection type inside.
	 */
	float getNearPlane() const noexcept;

	// TODO: add perspective/ortho morphing.
};

/**
 * @brief Contains information about camera animation frame.
 */
struct CameraFrame final : public AnimationFrame
{
	struct BaseFrame final
	{
		ProjectionType type = ProjectionType::Perspective;
		bool animate0 = false;
		bool animate1 = false;
		bool animate2 = false;
	};
	struct PerspectiveFrame final
	{
		ProjectionType type = ProjectionType::Perspective;
		bool animateFieldOfView = false;
		bool animateAspectRatio = false;
		bool animateNearPlane = false;
	};
	struct OrthographicFrame final
	{
		ProjectionType type = ProjectionType::Orthographic;
		bool animateWidth = false;
		bool animateHeight = false;
		bool animateDepth = false;
	};
	struct FrameProjection final
	{
		BaseFrame base;
		PerspectiveFrame perspective;
		OrthographicFrame orthographic;
		FrameProjection() : perspective() { }
	};

private:
	uint8 _alignment0 = 0;
	uint16 _alignment1 = 0;
public:
	CameraProjection c = {};
	FrameProjection f = {};
};

/***********************************************************************************************************************
 * @brief Handles camera projections.
 */
class CameraSystem final : public System, public ISerializable, public IAnimatable
{
	LinearPool<CameraComponent, false> components;
	LinearPool<CameraFrame, false> animationFrames;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(ID<Component> source, ID<Component> destination) final;
	
	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(ID<Entity> entity, ID<AnimationFrame> a, ID<AnimationFrame> b, float t) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;

	friend class ecsm::Manager;
public:
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
};

} // namespace garden
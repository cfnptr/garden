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
 */

#pragma once
#include "math/angles.hpp"
#include "garden/serialize.hpp"

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

/***********************************************************************************************************************
 */
struct CameraComponent final : public Component
{
	enum class Type : uint8
	{
		Perspective, Orthographic, Count,
	};
	struct Perspective final
	{
		float fieldOfView = radians(defaultFieldOfView);
		float aspectRatio = defaultAspectRatio;
		float nearPlane = defaultHmdDepth;
	};
	struct Orthographic final
	{
		float2 width = float2(-1.0f, 1.0f);
		float2 height = float2(-1.0f, 1.0f);
		float2 depth = float2(-1.0f, 1.0f);
	};
	union Projection final
	{
		Perspective perspective;
		Orthographic orthographic;
		Projection() : perspective() { }
	};

	Projection p = {};
	Type type = Type::Perspective;

private:
	uint8_t _alignment0 = 0;
	uint16_t _alignment1 = 0;
public:

	friend class CameraSystem;
	friend class LinearPool<CameraComponent, false>;

	float4x4 calcProjection() const noexcept;
	// TODO: add perspective/ortho morphing.
};

/***********************************************************************************************************************
 */
class CameraSystem final : public System, public ISerializable
{
	LinearPool<CameraComponent, false> components;

	#if GARDEN_EDITOR
	void* editor = nullptr;

	void init();
	void deinit();
	#endif

	CameraSystem(Manager* manager);
	~CameraSystem() final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	void serialize(ISerializer& serializer, ID<Entity> entity) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity) final;

	friend class ecsm::Manager;
	friend class CameraEditor;
};

} // namespace garden
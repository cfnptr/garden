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
#include "ecsm.hpp"
#include "math/angles.hpp"
#include "math/matrix.hpp"

namespace garden
{

// Head-mounted display size.
#define DEFAULT_HMD_DEPTH 0.01f
#define DEFAULT_FIELD_OF_VIEW 90.0f
#define DEFAULT_ASPECT_RATIO (16.0f / 9.0f)

using namespace math;
using namespace ecsm;

//--------------------------------------------------------------------------------------------------
struct CameraComponent final : public Component
{
	enum class Type : uint8
	{
		Perspective, Orthographic, Count,
	};
	struct Perspective final
	{
		float fieldOfView = radians(DEFAULT_FIELD_OF_VIEW);
		float aspectRatio = DEFAULT_ASPECT_RATIO;
		float nearPlane = DEFAULT_HMD_DEPTH;
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

	friend class CameraSystem;
	friend class LinearPool<CameraComponent, false>;

	float4x4 calcProjection() const noexcept;
	// TODO: add perspective/ortho morphing.
};

//--------------------------------------------------------------------------------------------------
class CameraSystem final : public System
{
	LinearPool<CameraComponent, false> components;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	friend class ecsm::Manager;
	friend class CameraEditor;
};

} // garden
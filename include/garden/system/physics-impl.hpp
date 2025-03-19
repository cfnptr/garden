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

#pragma once
#include "math/aabb.hpp"
#include "math/color.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"

#include "Jolt/Jolt.h"
#include "Jolt/Core/Color.h"
#include "Jolt/Geometry/AABox.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"

namespace garden::physics
{

using namespace math;

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
	constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	constexpr JPH::BroadPhaseLayer MOVING(1);
	constexpr JPH::uint NUM_LAYERS(2);
};

static f32x4 toF32x4(const JPH::Vec3& v) noexcept { return f32x4(v.mValue); }
static f32x4 toF32x4(const JPH::Vec4& v) noexcept { return f32x4(v.mValue); }
static u32x4 toU32x4(const JPH::UVec4& v) noexcept { return u32x4(v.mValue); }
static quat toQuat(const JPH::Quat& q) noexcept { return quat(q.mValue.mValue); }
static Aabb toAabb(const JPH::AABox& aabb) noexcept { return Aabb(aabb.mMin.mValue, aabb.mMax.mValue); }

static JPH::Vec3 toVec3(f32x4 v) noexcept { return JPH::Vec3(v.getX(), v.getY(), v.getZ()); } // Fixing W
static JPH::RVec3 toRVec3(f32x4 v) noexcept { return JPH::RVec3(v.getX(), v.getY(), v.getZ()); } // Fixing W
static JPH::Vec4 toVec4(f32x4 v) noexcept { return JPH::Vec4(v.data); }
static JPH::UVec4 toUVec4(u32x4 v) noexcept { return JPH::UVec4(v.data); }
static JPH::Quat toQuat(quat q) noexcept { return JPH::Quat(q.data); }
static JPH::AABox toAABox(const Aabb& aabb) noexcept { return JPH::AABox(aabb.getMin().data, aabb.getMax().data); }

static f32x4x4 toF32x4x4(const JPH::Mat44& m) noexcept
{
	return f32x4x4(toF32x4(m.GetColumn4(0)), toF32x4(m.GetColumn4(1)),
		toF32x4(m.GetColumn4(2)), toF32x4(m.GetColumn4(3)));
}
static JPH::Mat44 toMat44(const f32x4x4& m) noexcept
{
	return JPH::Mat44(toVec4(m.c0), toVec4(m.c1), toVec4(m.c2), toVec4(m.c3));
}

static math::Color toMathColor(JPH::ColorArg color) noexcept
{
	return *((const math::Color*)&color.mU32);
}

} // namespace garden::physics
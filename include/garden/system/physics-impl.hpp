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

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"

namespace garden::physics
{

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr JPH::uint NUM_LAYERS(2);
};

static float3 toFloat3(const JPH::Vec3& v) noexcept
{
	return float3(v.GetX(), v.GetY(), v.GetZ());
}
static quat toQuat(const JPH::Quat& q) noexcept
{
	return quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

static JPH::Vec3 toVec3(const float3& v) noexcept
{
	return JPH::Vec3(v.x, v.y, v.z);
}
static JPH::RVec3 toRVec3(const float3& v) noexcept
{
	return JPH::RVec3(v.x, v.y, v.z);
}
static JPH::Quat toQuat(const quat& q) noexcept
{
	return JPH::Quat(q.x, q.y, q.z, q.w);
}

}
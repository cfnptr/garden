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
#include "math/vector.hpp"
#include <array>

namespace garden::graphics::primitive
{

using namespace std;
using namespace math;

//--------------------------------------------------------------------------------------------------
static const array<float2, 4> oneSquareVert2D =
{
	float2(-0.5f, -0.5f), float2(-0.5f,  0.5f),
	float2( 0.5f,  0.5f), float2( 0.5f, -0.5f)
};
// TODO: uv
static const array<float3, 4> oneSquareVert3D =
{
	float3(-0.5f, -0.5f, 0.0f), float3(-0.5f,  0.5f, 0.0f),
	float3( 0.5f,  0.5f, 0.0f), float3( 0.5f, -0.5f, 0.0f)
};

static const array<float2, 4> twoSquareVert2D =
{
	float2(-1.0f, -1.0f), float2(-1.0f,  1.0f),
	float2( 1.0f,  1.0f), float2( 1.0f, -1.0f)
};
static const array<float2, 8> twoSquareVertUv2D =
{
	float2(-1.0f, -1.0f), float2(0.0f, 0.0f), float2(-1.0f,  1.0f), float2(0.0f, 1.0f),
	float2( 1.0f,  1.0f), float2(1.0f, 1.0f), float2( 1.0f, -1.0f), float2(1.0f, 0.0f)
};
static const array<float3, 4> twoSquareVert3D =
{
	float3(-1.0f, -1.0f, 0.0f), float3(-1.0f,  1.0f, 0.0f),
	float3( 1.0f,  1.0f, 0.0f), float3( 1.0f, -1.0f, 0.0f)
};

static const array<uint16, 6> squareInd16 = { 0, 1, 2, 0, 2, 3 };

//--------------------------------------------------------------------------------------------------
static const array<float3, 24> oneCubeVert =
{
	float3(-0.5f, -0.5f,  0.5f), float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f, -0.5f), float3(-0.5f, -0.5f, -0.5f),

	float3( 0.5f, -0.5f, -0.5f), float3( 0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f,  0.5f), float3( 0.5f, -0.5f,  0.5f),

	float3(-0.5f, -0.5f,  0.5f), float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f, -0.5f), float3( 0.5f, -0.5f,  0.5f),

	float3(-0.5f,  0.5f, -0.5f), float3(-0.5f,  0.5f,  0.5f),
	float3( 0.5f,  0.5f,  0.5f), float3( 0.5f,  0.5f, -0.5f),

	float3(-0.5f, -0.5f, -0.5f), float3(-0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f, -0.5f), float3( 0.5f, -0.5f, -0.5f),

	float3( 0.5f, -0.5f,  0.5f), float3( 0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f,  0.5f), float3(-0.5f, -0.5f,  0.5f)
};
static const array<float3, 24> twoCubeVert =
{
	float3(-1.0f, -1.0f,  1.0f), float3(-1.0f,  1.0f,  1.0f),
	float3(-1.0f,  1.0f, -1.0f), float3(-1.0f, -1.0f, -1.0f),

	float3( 1.0f, -1.0f, -1.0f), float3( 1.0f,  1.0f, -1.0f),
	float3( 1.0f,  1.0f,  1.0f), float3( 1.0f, -1.0f,  1.0f),

	float3(-1.0f, -1.0f,  1.0f), float3(-1.0f, -1.0f, -1.0f),
	float3( 1.0f, -1.0f, -1.0f), float3( 1.0f, -1.0f,  1.0f),

	float3(-1.0f,  1.0f, -1.0f), float3(-1.0f,  1.0f,  1.0f),
	float3( 1.0f,  1.0f,  1.0f), float3( 1.0f,  1.0f, -1.0f),

	float3(-1.0f, -1.0f, -1.0f), float3(-1.0f,  1.0f, -1.0f),
	float3( 1.0f,  1.0f, -1.0f), float3( 1.0f, -1.0f, -1.0f),

	float3( 1.0f, -1.0f,  1.0f), float3( 1.0f,  1.0f,  1.0f),
	float3(-1.0f,  1.0f,  1.0f), float3(-1.0f, -1.0f,  1.0f)
};
static const array<float3, 48> oneCubeVertNorm =
{
	float3(-0.5f, -0.5f,  0.5f), float3::left, float3(-0.5f,  0.5f,  0.5f), float3::left,
	float3(-0.5f,  0.5f, -0.5f), float3::left, float3(-0.5f, -0.5f, -0.5f), float3::left,

	float3( 0.5f, -0.5f, -0.5f), float3::right, float3(0.5f,  0.5f, -0.5f), float3::right,
	float3( 0.5f,  0.5f,  0.5f), float3::right, float3(0.5f, -0.5f,  0.5f), float3::right,

	float3(-0.5f, -0.5f,  0.5f), float3::bottom, float3(-0.5f, -0.5f, -0.5f), float3::bottom,
	float3( 0.5f, -0.5f, -0.5f), float3::bottom, float3( 0.5f, -0.5f,  0.5f), float3::bottom,

	float3(-0.5f,  0.5f, -0.5f), float3::top, float3(-0.5f, 0.5f,  0.5f), float3::top,
	float3( 0.5f,  0.5f,  0.5f), float3::top, float3( 0.5f, 0.5f, -0.5f), float3::top,

	float3(-0.5f, -0.5f, -0.5f), float3::back, float3(-0.5f,  0.5f, -0.5f), float3::back,
	float3( 0.5f,  0.5f, -0.5f), float3::back, float3( 0.5f, -0.5f, -0.5f), float3::back,

	float3( 0.5f, -0.5f,  0.5f), float3::front, float3( 0.5f,  0.5f, 0.5f), float3::front,
	float3(-0.5f,  0.5f,  0.5f), float3::front, float3(-0.5f, -0.5f, 0.5f), float3::front
};
// TODO: uv

static const array<uint16, 36> cubeInd16 =
{
	0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11,
	12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
};

//--------------------------------------------------------------------------------------------------
// TODO: test different layouts to find fastest.
static const array<float3, 36> fullCubeVert =
{
	// -X side
	float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f, -0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	// -Z side
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f,  0.5f, -0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f, -0.5f),
	// -Y side
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f, -0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	// +Y side
	float3(-0.5f,  0.5f, -0.5f),
	float3(-0.5f,  0.5f,  0.5f),
	float3( 0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f,  0.5f),
	float3( 0.5f,  0.5f, -0.5f),
	// +X side
	float3( 0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f,  0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f, -0.5f),
	float3( 0.5f,  0.5f, -0.5f),
	// +Z side
	float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.5f,  0.5f,  0.5f),
	float3(-0.5f, -0.5f,  0.5f),
	float3( 0.5f, -0.5f,  0.5f),
	float3( 0.5f,  0.5f,  0.5f),
};

} // namespace garden::graphics::primitive
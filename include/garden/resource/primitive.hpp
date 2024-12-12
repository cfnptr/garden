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

#pragma once
#include "math/vector.hpp"
#include <array>

namespace garden::primitive
{

using namespace math;

//**********************************************************************************************************************
constexpr array<float2, 6> quadVertices =
{
	float2(-0.5f, -0.5f), float2(0.5f, -0.5f), float2(-0.5f,  0.5f),
	float2(-0.5f,  0.5f), float2(0.5f, -0.5f), float2( 0.5f,  0.5f)
};
const array<float2, 6> quadTexCoords =
{
	float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
	float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f)
};

constexpr array<float3, 36> cubeVertices =
{
	// -Y side
	float3(-0.5f, -0.5f,  0.5f), float3( 0.5f, -0.5f,  0.5f), float3(-0.5f, -0.5f, -0.5f),
	float3(-0.5f, -0.5f, -0.5f), float3( 0.5f, -0.5f,  0.5f), float3( 0.5f, -0.5f, -0.5f),
	// -Z side
	float3(-0.5f, -0.5f, -0.5f), float3( 0.5f, -0.5f, -0.5f), float3(-0.5f,  0.5f, -0.5f),
	float3(-0.5f,  0.5f, -0.5f), float3( 0.5f, -0.5f, -0.5f), float3( 0.5f,  0.5f, -0.5f),
	// +X side
	float3( 0.5f, -0.5f, -0.5f), float3( 0.5f, -0.5f,  0.5f), float3( 0.5f,  0.5f, -0.5f),
	float3( 0.5f,  0.5f, -0.5f), float3( 0.5f, -0.5f,  0.5f), float3( 0.5f,  0.5f,  0.5f),
	// +Z side
	float3( 0.5f, -0.5f,  0.5f), float3(-0.5f, -0.5f,  0.5f), float3( 0.5f,  0.5f,  0.5f),
	float3( 0.5f,  0.5f,  0.5f), float3(-0.5f, -0.5f,  0.5f), float3(-0.5f,  0.5f,  0.5f),
	// -X side
	float3(-0.5f, -0.5f,  0.5f), float3(-0.5f, -0.5f, -0.5f), float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f,  0.5f), float3(-0.5f, -0.5f, -0.5f), float3(-0.5f,  0.5f, -0.5f),
	// +Y side
	float3(-0.5f,  0.5f, -0.5f), float3( 0.5f,  0.5f, -0.5f), float3(-0.5f,  0.5f,  0.5f),
	float3(-0.5f,  0.5f,  0.5f), float3( 0.5f,  0.5f, -0.5f), float3( 0.5f,  0.5f,  0.5f)
};

} // namespace garden::primitive
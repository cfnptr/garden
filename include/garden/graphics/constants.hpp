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
#include "math/matrix.hpp"

namespace garden::graphics
{

using namespace math;

struct CameraConstants final
{
	float4x4 view = float4x4(0.0f);
	float4x4 projection = float4x4(0.0f);
	float4x4 viewProj = float4x4(0.0f);
	float4x4 viewInverse = float4x4(0.0f);
	float4x4 projInverse = float4x4(0.0f);
	float4x4 viewProjInv = float4x4(0.0f);
	float4 cameraPos = float4(0.0f);
	float4 viewDir = float4(0.0f);
	float4 lightDir = float4(0.0f);
	float2 frameSize = float2(0.0f);
	float2 frameSizeInv = float2(0.0f);
	float2 frameSizeInv2 = float2(0.0f);
	float nearPlane = 0.0f;
};

} // namespace garden::graphics

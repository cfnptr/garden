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

#include "common/depth.gsl"
#include "common/constants.gsl"
#include "common/fullscreen.gsl"

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;

out float3 fs.nearPoint;
out float3 fs.farPoint;

void main()
{
	float2 texCoords = toFullscreenTexCoords(gl.vertexIndex);
	fs.nearPoint = calcWorldPosition(1.0f, texCoords, cc.invViewProj) + cc.cameraPos.xyz;
	fs.farPoint = calcWorldPosition(0.0001f, texCoords, cc.invViewProj) + cc.cameraPos.xyz; // 0.001 is for inf far plane
	gl.position = float4(toFullscreenPosition(texCoords), 1.0f);
}
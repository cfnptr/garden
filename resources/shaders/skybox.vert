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

spec const float FAR_DEPTH_VALUE = 0.0f;

#include "common/primitives.gsl"

out float3 fs.texCoords;

uniform pushConstants
{
	float4x4 viewProj;
} pc;

void main()
{
	float3 vertexPos = cubeVertices[gl.vertexIndex];
	float4 position = pc.viewProj * float4(vertexPos, 1.0f);
	gl.position = float4(position.xy, FAR_DEPTH_VALUE, position.w); // Puts skybox on the far plane.
	fs.texCoords = vertexPos * 2.0f;
}
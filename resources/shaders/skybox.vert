//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#include "common/constants.gsl"

in float3 vs.position : f32;
out float3 fs.texCoords;

uniform pushConstants
{
	float4x4 viewProj;
} pc;

//--------------------------------------------------------------------------------------------------
void main()
{
	float4 position = pc.viewProj * float4(vs.position, 1.0f);
	// Puts skybox on the far plane. (reversed Z depth)
	gl.position = float4(position.xy, 0.0f, position.w);
	fs.texCoords = vs.position * 2.0f;
}
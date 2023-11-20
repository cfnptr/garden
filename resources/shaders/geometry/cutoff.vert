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

#include "geometry/common.gsl"
#include "common/constants.gsl"

in float3 vs.position : f32;
in float3 vs.normal : f32;
in float2 vs.texCoords : f32;

out float2 fs.texCoords;
out float3 fs.normal;

uniform pushConstants
{
	float4 baseColor;
	float4 emissive;
	float metallic;
	float roughness;
	float reflectance;
	uint32 instanceIndex;
	float cutoff;
} pc;

buffer readonly Instance
{
	InstanceData data[];
} instance;

//--------------------------------------------------------------------------------------------------
void main()
{
	gl.position = instance.data[pc.instanceIndex].mvp * float4(vs.position, 1.0f);
	fs.normal = calcNormal(instance.data[pc.instanceIndex].model, vs.normal);
	fs.texCoords = vs.texCoords;
}
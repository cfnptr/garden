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

#include "common/gbuffer.gsl"

// TODO: share the one opaque pipeline using #variantCount 2

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

in float2 fs.texCoords;
in float3 fs.normal;

out float4 fb.gBuffer0;
out float4 fb.gBuffer1;
out float4 fb.gBuffer2;

uniform pushConstants
{
	float4 baseColor;
	float4 emissive;
	float metallic;
	float roughness;
	float reflectance;
	uint32 instanceIndex;
	float alphaCutoff;
} pc;

uniform set1 sampler2D
{
	filter = linear;
	wrap = repeat;
} baseColorMap;
uniform set1 sampler2D
{
	filter = linear;
	wrap = repeat;
} ormMap;

//--------------------------------------------------------------------------------------------------
void main()
{
	float4 color = texture(baseColorMap, fs.texCoords) * pc.baseColor;
	if (color.a < pc.alphaCutoff) discard;
	float4 orm = texture(ormMap, fs.texCoords);
	fb.gBuffer0 = encodeGBuffer0(color.rgb, orm.b * pc.metallic); 
	fb.gBuffer1 = encodeGBuffer1(fs.normal, pc.reflectance);
	fb.gBuffer2 = encodeGBuffer2(pc.emissive.rgb, orm.g * pc.roughness);
}
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

// TODO: or maybe we can utilize filament micro/macro AO?

#define USE_AMBIENT_OCCLUSION
#define USE_CLEAR_COAT
#define USE_LIGHT_EMISSION
#define USE_SPECULAR_FACTOR
#define USE_EMISSION_BUFFER
#define USE_GI_BUFFER

spec const bool HAS_SHADOW_BUFFER = false;
spec const bool HAS_AO_BUFFER = false;
spec const bool HAS_REFLECTION_BUFFER = false;
spec const bool HAS_EMISSION_BUFFER = false;
spec const bool HAS_GI_BUFFER = false;

#include "common/pbr.gsl"
#include "common/depth.gsl"

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.hdr;

uniform sampler2D g0;
uniform sampler2D g1;
uniform sampler2D g2;
uniform sampler2D g3;
uniform sampler2D g4;
uniform sampler2D g5;

uniform sampler2D depthBuffer;
uniform sampler2D shadowBuffer;
uniform sampler2D aoBuffer;

uniform sampler2D
{
	filter = linear;
} reflBuffer;

uniform sampler2D
{
	filter = linear;
} dfgLUT;

uniform set1 samplerCube
{
	filter = linear;
} specular;
uniform set1 IblData
{
	float4 data[SH_COEFF_COUNT];
} sh;

uniform pushConstants
{
	float4x4 uvToWorld;
	float4 shadowColor;
	float reflLodOffset;
	float emissiveCoeff;
	float reflectanceCoeff;
} pc;

//**********************************************************************************************************************
void main()
{
	float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).r;
	if (depth == 0.0f)
		discard;

	GBufferValues values = DECODE_G_BUFFER_VALUES(fs.texCoords);
	values.emissiveFactor *= pc.emissiveCoeff;
	values.reflectance *= pc.reflectanceCoeff;
	
	float4 shadow = float4(pc.shadowColor.rgb, values.shadow);
	if (HAS_SHADOW_BUFFER)
	{
		float4 accumShadow = textureLod(shadowBuffer, fs.texCoords, 0.0f);
		shadow.a = min(shadow.a, accumShadow.a);
		shadow.rgb *= accumShadow.rgb;
	}
	shadow.rgb *= mix(pc.shadowColor.a, 1.0f, shadow.a);

	float4 worldPosition = pc.uvToWorld * float4(fs.texCoords, depth, 1.0f);
	worldPosition.xyz /= worldPosition.w;

	if (HAS_AO_BUFFER)
	{
		float ao = textureLod(aoBuffer, fs.texCoords, 0.0f).r;
		values.ambientOcclusion = min(values.ambientOcclusion, ao);
	}
	if (HAS_REFLECTION_BUFFER)
	{
		float lod = calcReflectionsLod(values.roughness, length(worldPosition.xyz), pc.reflLodOffset);
		values.reflectionColor = textureLod(reflBuffer, fs.texCoords, lod);
	}

	float3 viewDirection = calcViewDirection(worldPosition.xyz);
	float3 hdrColor = evaluateIBL(values, shadow, viewDirection, dfgLUT, sh.data, specular);
	fb.hdr = float4(hdrColor, 1.0f);
}
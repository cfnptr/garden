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

spec const bool USE_SHADOW_BUFFER = false;
spec const bool USE_AO_BUFFER = false;
spec const bool USE_REFLECTION_BUFFER = false;
spec const bool USE_EMISSIVE_BUFFER = false;
spec const bool USE_GI_BUFFER = false;

// TODO: or maybe we can utilize filament micro/macro AO?

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
	float4 shadow;
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

	GBufferValues gBuffer = DECODE_G_BUFFER_VALUES(fs.texCoords);
	gBuffer.reflectance *= pc.reflectanceCoeff;
	
	float4 shadow = float4(pc.shadow.rgb, gBuffer.shadow);
	if (USE_SHADOW_BUFFER)
	{
		float4 accumShadow = textureLod(shadowBuffer, fs.texCoords, 0.0f);
		shadow.a = min(shadow.a, accumShadow.a);
		shadow.rgb *= accumShadow.rgb;
	}
	shadow.rgb *= mix(pc.shadow.a, 1.0f, shadow.a);

	float4 worldPosition = pc.uvToWorld * float4(fs.texCoords, depth, 1.0f);
	worldPosition.xyz /= worldPosition.w;
	float3 viewDirection = calcViewDirection(worldPosition.xyz);

	if (USE_AO_BUFFER)
	{
		float ao = textureLod(aoBuffer, fs.texCoords, 0.0f).r;
		gBuffer.ambientOcclusion = min(gBuffer.ambientOcclusion, ao);
	}
	if (USE_REFLECTION_BUFFER)
	{
		float lod = reflectionsLod(gBuffer.roughness, length(worldPosition.xyz), pc.reflLodOffset);
		gBuffer.reflectionColor = textureLod(reflBuffer, fs.texCoords, lod);
	}

	float3 hdrColor = evaluateIBL(gBuffer, shadow, viewDirection, dfgLUT, sh.data, specular);
	if (USE_EMISSIVE_BUFFER)
		hdrColor += gBuffer.emissiveColor * gBuffer.emissiveFactor * pc.emissiveCoeff;
	fb.hdr = float4(hdrColor, 1.0f);
}
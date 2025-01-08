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

#include "common/pbr.gsl"
#include "common/depth.gsl"

spec const bool USE_SHADOW_BUFFER = false;
spec const bool USE_AO_BUFFER = false;

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

uniform sampler2D depthBuffer;
uniform sampler2D shadowBuffer;
uniform sampler2D aoBuffer;

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
	float4 data[SH_COEF_COUNT];
} sh;

uniform pushConstants
{
	float4x4 uvToWorld;
	float4 shadowEmissive;
} pc;

//**********************************************************************************************************************
void main()
{
	float depth = texture(depthBuffer, fs.texCoords).x;
	if (depth < FLOAT_EPS6)
		discard;

	GBufferValues gBuffer = decodeGBufferValues(g0, g1, g2, g3, g4, fs.texCoords);
	float4 shadow = float4(pc.shadowEmissive.rgb, 
		USE_SHADOW_BUFFER ? texture(shadowBuffer, fs.texCoords).r : 1.0f);
	float ao = USE_AO_BUFFER ? texture(aoBuffer, fs.texCoords).r : 1.0f;
	gBuffer.ambientOcclusion = min(gBuffer.ambientOcclusion, ao); // TODO: or maybe we can utilize filament micro/macro AO?

	float4 worldPosition = pc.uvToWorld * float4(fs.texCoords, depth, 1.0f);
	float3 viewDirection = calcViewDirection(worldPosition.xyz / worldPosition.w);

	float3 hdrColor = float3(0.0f);
	hdrColor += evaluateIBL(gBuffer, shadow, viewDirection, dfgLUT, sh.data, specular);
	hdrColor += gBuffer.emissiveColor * gBuffer.emissiveFactor * pc.shadowEmissive.a;

	float obstruction = depth < (1.0f - FLOAT_EPS6) ? 1.0f : 0.0f;
	fb.hdr = float4(hdrColor * obstruction, 1.0f);
	
	// TODO: temporal. integrate ssao into the pbr like it's done in the filament.
	//fb.hdr *= texture(aoBuffer, fs.texCoords).r;
}
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

#define USE_SPECULAR_FACTOR
#define USE_AMBIENT_OCCLUSION
#define USE_LIGHT_SHADOW
#define USE_CLEAR_COAT
#define USE_LIGHT_EMISSION

spec const bool USE_CLEAR_COAT_BUFFER = false;
spec const bool USE_EMISSION_BUFFER = false;

#include "common/gbuffer.gsl"

pipelineState
{
	faceCulling = off;
	colorMask2 = r;
	colorMask3 = r;
}

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;
out float4 fb.g4;
out float4 fb.g5;

uniform pushConstants
{
	float3 baseColor;
	float specularFactor;
	float4 mraor;
	float3 emissiveColor;
	float emissiveFactor;
	float shadow;
	float ccRoughness;
} pc;

void main()
{
	GBufferValues gBuffer = gBufferValuesDefault();
	gBuffer.baseColor = pc.baseColor;
	gBuffer.specularFactor = pc.specularFactor;
	gBuffer.metallic = pc.mraor.r;
	gBuffer.roughness = pc.mraor.g;
	gBuffer.ambientOcclusion = pc.mraor.b;
	gBuffer.reflectance = pc.mraor.a;
	gBuffer.shadow.a = pc.shadow;
	gBuffer.clearCoatRoughness = pc.ccRoughness;
	gBuffer.emissiveColor = pc.emissiveColor;
	gBuffer.emissiveFactor = pc.emissiveFactor;
	ENCODE_G_BUFFER_VALUES(gBuffer);
}
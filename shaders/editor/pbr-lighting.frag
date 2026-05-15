// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#define USE_SHADOW_ALPHA
#define USE_CLEAR_COAT
#define USE_LIGHT_EMISSION

#include "common/gbuffer.gsl"

pipelineState
{
	// Note: should match G-Buffer!
	colorMask2 = r;
	colorMask3 = none;
}

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;

uniform pushConstants
{
	float3 baseColor;
	float specularFactor;
	float4 mraor;
	float shadowAlpha;
	float emissiveFactor;
	float clearCoat;
	float clearCoatRoughness;
	uint32 materialID;
} pc;

void main()
{
	GBufferValues gBuffer = gBufferValuesDefault();
	gBuffer.materialID = pc.materialID;
	gBuffer.baseColor = float4(pc.baseColor, 1.0f);
	gBuffer.specularFactor = pc.specularFactor;
	gBuffer.metallic = pc.mraor.r;
	gBuffer.roughness = pc.mraor.g;
	gBuffer.ambientOcclusion = pc.mraor.b;
	gBuffer.reflectance = pc.mraor.a;
	gBuffer.shadowAlpha = pc.shadowAlpha;
	gBuffer.clearCoat = pc.clearCoat;
	gBuffer.clearCoatRoughness = pc.clearCoatRoughness;
	gBuffer.emissiveColor = float4(pc.baseColor, pc.emissiveFactor);
	ENCODE_G_BUFFER_VALUES(gBuffer);
}
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
#include "common/gbuffer.gsl"
#include "common/constants.gsl"

pipelineState
{
	faceCulling = off;
}

in float2 fs.texCoords;
out float4 fb.hdr;

spec const bool USE_SHADOW_BUFFER = false;
spec const bool USE_AO_BUFFER = false;

uniform sampler2D gBuffer0;
uniform sampler2D gBuffer1;
uniform sampler2D gBuffer2;
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
	float4 sh[SH_COEF_COUNT];
} data;

uniform pushConstants
{
	float4x4 uvToWorld;
	float4 shadowColor;
} pc;

//**********************************************************************************************************************
void main()
{
	float depth = texture(depthBuffer, fs.texCoords).x;
	if (depth < FLOAT_EPS6)
		discard;

	float4 gData0 = texture(gBuffer0, fs.texCoords);
	float4 gData1 = texture(gBuffer1, fs.texCoords);
	float4 gData2 = texture(gBuffer2, fs.texCoords);
	float shadow = USE_SHADOW_BUFFER ? texture(shadowBuffer, fs.texCoords).r : 1.0f;
	float ambientOcclusion = USE_AO_BUFFER ? texture(aoBuffer, fs.texCoords).r : 1.0f;
	float4 worldPos = pc.uvToWorld * float4(fs.texCoords, depth, 1.0f);
	float3 viewDir = calcViewDirection(worldPos.xyz / worldPos.w);

	PbrMaterial pbrMaterial; // TODO: define emissive strength in candela, lumens or wats.
	pbrMaterial.baseColor = decodeBaseColor(gData0);
	pbrMaterial.metallic = decodeMetallic(gData0);
	pbrMaterial.roughness = decodeRoughness(gData2);
	pbrMaterial.reflectance = decodeReflectance(gData1);
	pbrMaterial.ambientOcclusion = ambientOcclusion;
	pbrMaterial.viewDirection = viewDir;
	pbrMaterial.normal = decodeNormal(gData1);
	pbrMaterial.shadow = shadow;
	pbrMaterial.shadowColor = pc.shadowColor.rgb;

	float3 hdrColor = float3(0.0f);
	hdrColor += evaluateIBL(pbrMaterial, dfgLUT, data.sh, specular);
	hdrColor += decodeEmissive(gData2);

	float obstruction = depth < (1.0f - FLOAT_EPS6) ? 1.0f : 0.0f;
	fb.hdr = float4(hdrColor * obstruction, 1.0f);
	
	// TODO: temporal. integrate ssao into the pbr like it's done in the filament.
	//fb.hdr *= texture(aoBuffer, fs.texCoords).r;
}
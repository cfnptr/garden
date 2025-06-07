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

#define USE_SHADOW_BUFFER true
#define USE_SUB_SURFACE_SCATTERING false
#define SHADOW_MAP_CASCADE_COUNT 3 // TODO: allow to change

#include "model/common.gsl"
#include "common/oit.gsl"
#include "common/pbr.gsl"
#include "common/csm.gsl"
#include "common/depth.gsl"
#include "common/constants.gsl"

pipelineState
{
	depthTesting = on;
	blending0 = on;
	srcBlendFactor0 = one;
	dstBlendFactor0 = one;
	blending1 = on;
	srcBlendFactor1 = zero;
	dstBlendFactor1 = oneMinusSrcColor;
}

in float2 fs.texCoords;
in float3 fs.worldPos;
in float3x3 fs.tbn;

out float4 fb.accum;
out float fb.reveal;

//**********************************************************************************************************************
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} colorMap;
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} mraorMap;
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} normalMap;
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} emissiveMap;

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

uniform sampler2DArrayShadow
{
	comparison = on;
	filter = linear;
	addressMode = clampToBorder;
} shadowMap;
uniform ShadowData
{
	SHADOW_MAP_DATA
} shadowData;

uniform CameraConstants
{
	CAMERA_CONSTANTS
} cc;

//**********************************************************************************************************************
void main()
{
	float4 baseColor = texture(colorMap, fs.texCoords);
	GBufferValues values = fillModelGBuffer(mraorMap, normalMap, emissiveMap, fs.tbn, fs.texCoords, baseColor);
	values.specularFactor = 1.0f; values.ambientOcclusion = 1.0f; values.transmission = 0.0f;

	float4 shadow = float4(cc.shadowColor.rgb, 1.0f);
	if (gl.fragCoord.z >= shadowData.farPlanes.z)
	{
		uint32 cascadeID; float3 lightCoords;
		computeCsmData(shadowData.lightSpace, gl.fragCoord.xy * cc.invFrameSize, 
			gl.fragCoord.z, shadowData.farPlanes.xyz, shadowData.lightDirBias.xyz,
			shadowData.lightDirBias.w, values.normal, cascadeID, lightCoords);
		shadow.a = evaluateCsmShadows(shadowMap, cascadeID, lightCoords);
		shadow.rgb *= mix(cc.shadowColor.a, 1.0f, shadow.a);
	}

	float3 viewDirection = calcViewDirection(fs.worldPos);
	float3 hdrColor = evaluateIBL(values, shadow, viewDirection, dfgLUT, sh.data, specular);
	hdrColor += values.emissiveColor * values.emissiveFactor * cc.emissiveCoeff * baseColor.a;
	computeOIT(float4(hdrColor, baseColor.a), gl.fragCoord.z, fb.accum, fb.reveal);
}
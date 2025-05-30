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

// TODO: support regular depth buffer. spec const float FAR_DEPTH_VALUE = 0.0f;
#define USE_EMISSIVE_BUFFER false 
#define USE_GI_BUFFER false 
#define SHADOW_MAP_CASCADE_COUNT 3 // TODO: allow to use less cascade count

#include "common/csm.gsl"
#include "common/gbuffer.gsl"

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.shadow;

uniform sampler2D gBufferNormals;
uniform sampler2D depthBuffer;

uniform sampler2DArrayShadow
{
	comparison = on;
	filter = linear;
	addressMode = clampToBorder;
} shadowMap;
uniform sampler2DArray
{
	filter = linear;
	addressMode = clampToBorder;
} transparentMap;

uniform ShadowData
{
	SHADOW_MAP_DATA
} shadowData;

//**********************************************************************************************************************
void main()
{
	float pixelDepth = texture(depthBuffer, fs.texCoords).r;
	if (pixelDepth < shadowData.farPlanesIntens.z)
		discard;

	float3 normal = decodeNormal(texture(gBufferNormals, fs.texCoords));
	uint32 cascadeID; float3 lightCoords;
	computeCsmData(shadowData.lightSpace, fs.texCoords, pixelDepth, shadowData.farPlanesIntens.xyz, 
		shadowData.lightDirBias.xyz, shadowData.lightDirBias.w, normal, cascadeID, lightCoords);
	float shadow = evaluateCsmShadows(shadowMap, cascadeID, lightCoords);

	float3 transparency = evaluateCsmTransparency(transparentMap, cascadeID, lightCoords);
	transparency = mix(transparency, float3(1.0f), shadow);
	fb.shadow = float4(transparency, 1.0f - shadow * shadowData.farPlanesIntens.w);
}
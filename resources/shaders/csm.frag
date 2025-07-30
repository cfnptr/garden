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

#define SHADOW_MAP_CASCADE_COUNT 3 // TODO: allow to use less cascade count

#include "common/csm.gsl"
#include "common/gbuffer.gsl"

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.shadow;

uniform sampler2D gNormals;
uniform sampler2D depthBuffer;

uniform sampler2DArrayShadow
{
	comparison = on;
	filter = linear;
	addressMode = clampToBorder;
} depthMap;
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
	float pixelDepth = textureLod(depthBuffer, fs.texCoords, 0.0f).r;
	if (pixelDepth < shadowData.farPlanes.z)
		discard;

	float3 normal = decodeNormal(textureLod(gNormals, fs.texCoords, 0.0f));
	uint32 cascadeID; float3 lightCoords;
	computeCsmData(shadowData.lightSpace, fs.texCoords, pixelDepth, shadowData.farPlanes.xyz, 
		shadowData.lightDirBias.xyz, shadowData.lightDirBias.w, normal, cascadeID, lightCoords);
	float shadow = evaluateCsmShadows(depthMap, cascadeID, lightCoords);

	float4 transparency = evaluateCsmTransparency(transparentMap, cascadeID, lightCoords);
	transparency = mix(float4(1.0f), transparency, shadow); // Note: Fix for peter-panning.
	fb.shadow = transparency * float4(float3(1.0f), shadow);
}
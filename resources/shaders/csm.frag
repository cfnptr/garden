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
#define SHADOW_MAP_CASCADE_COUNT 3 // TODO: allow to use less cascade count

#include "common/csm.gsl"
#include "common/math.gsl"

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float fb.shadow;

uniform sampler2D depthBuffer;

uniform sampler2DArrayShadow
{
	comparison = on;
	filter = linear;
	wrap = clampToBorder;
} shadowMap;

uniform ShadowData
{
	float4x4 lightSpace[SHADOW_MAP_CASCADE_COUNT];
	float4 farPlanesIntens;
} shadowData;

//**********************************************************************************************************************
void main()
{
	float pixelDepth = texture(depthBuffer, fs.texCoords).r;
	if (pixelDepth < shadowData.farPlanesIntens.z)
		discard;

	float shadow = computeCSM(shadowMap, shadowData.lightSpace, fs.texCoords, 
		pixelDepth, shadowData.farPlanesIntens.xyz, shadowData.farPlanesIntens.w);
	if (shadow < FLOAT_EPS6)
		discard;

	fb.shadow = 1.0f - shadow;
}
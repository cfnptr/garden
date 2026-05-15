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

#define USE_CAMERA_VOLUME
#define USE_CLOUDS_DEPTH
#define USE_CLOUDS_REPROJECTION

spec const float STEP_SIZE_FACTOR = 1.0f;
spec const float SLICE_COUNT = 8.0f;
spec const float KM_PER_SLICE = 12.0f;

#include "clouds/common.gsl"
#include "common/constants.gsl"

in noperspective float2 fs.texCoords;

out float4 fb.color;
out float4 fb.depth;

uniform sampler2D
{
	filter = linear;
} camView;
uniform sampler2D
{
	filter = linear;
} camViewDepth;

uniform sampler2D disocclMap;
uniform sampler2D hizBuffer;

uniform sampler2D
{
	filter = linear;
} transLUT;
uniform sampler3D
{
	filter = linear;
} cameraVolume;
uniform sampler2D
{
	addressMode = repeat;
	filter = linear;
} dataFields;
uniform sampler2D
{
	filter = linear;
} vertProfile;
uniform sampler3D
{
	addressMode = repeat;
	filter = linear;
} noiseShape;
uniform sampler2D
{
	addressMode = repeat;
	filter = linear;
} cirrusShape;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;

uniform pushConstants
{
	float3 cameraPos;
	float groundRadius;
	uint2 bayerPos;
	float atmTopRadius;
	float bottomRadius;
	float topRadius;
	float minDistance;
	float maxDistance;
	float currentTime;
	float cumulusCoverage;
	float cirrusCoverage;
	float temperatureDiff;
} pc;

//**********************************************************************************************************************
void main()
{
	CloudsParams clouds;
	clouds.viewProj = cc.viewProj;
	clouds.prevViewProj = cc.prevViewProj;
	clouds.fragCoord = uint2(gl.fragCoord.xy);
	clouds.bayerPos = pc.bayerPos;
	clouds.invViewProj = cc.invViewProj;
	clouds.starDir = -cc.lightDir;
	clouds.windDir = cc.windDir;
	clouds.starColor = float3(1.0f); // TODO:
	clouds.ambientLight = cc.ambientLight;
	clouds.cameraPos = pc.cameraPos;
	clouds.texCoords = fs.texCoords;
	clouds.groundRadius = pc.groundRadius;
	clouds.atmTopRadius = pc.atmTopRadius;
	clouds.bottomRadius = pc.bottomRadius;
	clouds.topRadius = pc.topRadius;
	clouds.minDistance = pc.minDistance;
	clouds.maxDistance = pc.maxDistance;
	clouds.currentTime = pc.currentTime;
	clouds.cumulusCoverage = pc.cumulusCoverage;
	clouds.cirrusCoverage = pc.cirrusCoverage;
	clouds.temperatureDiff = pc.temperatureDiff;
	clouds.stepSizeFactor = STEP_SIZE_FACTOR;

	float depth; evaluateClouds(transLUT, cameraVolume, dataFields, vertProfile, noiseShape, 
		cirrusShape, clouds, fb.color, camView, camViewDepth, disocclMap, hizBuffer, depth);
	fb.depth = float4(depth, float3(0.0f));
}
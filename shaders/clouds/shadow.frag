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

#include "common/depth.gsl"
#include "common/sphere.gsl"
#include "clouds/common.gsl"

pipelineState
{
	blending0 = on;
	colorMask0 = a;
	alphaOperation0 = minimum;
	blendFactor0 = one;
}

in noperspective float2 fs.texCoords;
out float4 fb.shadow;

uniform sampler2D hizBuffer;

uniform sampler2D
{
	addressMode = repeat;
	filter = linear;
} dataFields;

uniform pushConstants
{
	float4x4 invViewProj;
	float3 cameraPos;
	float bottomRadius;
	float3 starDir;
	float currentTime;
	float3 windDir;
	float cumulusCoverage;
	float temperatureDiff;
} pc;

//**********************************************************************************************************************
void main()
{
	float2 hizDepth = textureLod(hizBuffer, fs.texCoords, 0.0f).xy;
	float depth = MIN_HIZ(hizDepth) == FAR_PLANE_DEPTH ? MAX_HIZ(hizDepth) : MIN_HIZ(hizDepth);
	if (depth == FAR_PLANE_DEPTH)
		discard;

	float3 worldPos = fma(calcWorldPosition(depth, fs.texCoords, pc.invViewProj), float3(0.001f), pc.cameraPos);
	float2 rayT = raycast2(Sphere(float3(0.0f), pc.bottomRadius), Ray(worldPos, pc.starDir));
	
	if (!isIntersected(rayT)) // No bottom cloud layer intersection.
		discard;

	float3 samplePos = fma(pc.starDir, float3(rayT.x < 0.0f ? rayT.y : rayT.x), pc.cameraPos);
	float3 fieldWindDir = calcFieldWindDir(pc.windDir, pc.currentTime);
	float3 cloudData = sampleDataFields(dataFields, pc.cameraPos, samplePos, fieldWindDir, 0.02f);
	float shadow = 1.0f - calcCloudCoverage(pc.cumulusCoverage, cloudData) * pc.temperatureDiff;
	fb.shadow = float4(float3(1.0f), saturate(pow(shadow, 8.0f)));
}
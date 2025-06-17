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

#define USE_EMISSIVE_BUFFER false
#define USE_GI_BUFFER false

#include "ssao/defines.h"
#include "common/gbuffer.gsl"
#include "common/constants.gsl"

spec const uint32 STEP_COUNT = 4;

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.ao;

uniform sampler2D hizBuffer;

uniform sampler2D
{
	addressMode = repeat;
} noise;
uniform CameraConstants 
{
	CAMERA_CONSTANTS
} cc;

uniform pushConstants
{
	float4 projInfo;
	float2 invFullRes;
	float negInvR2;
	float radiusToScreen;
	float powExponent;
	float novBias;
	float aoMultiplier;
	uint32 projOrtho;
} pc;

//**********************************************************************************************************************
float3 texCoordsToView(float2 texCoords, float eyeZ)
{
	return float3(fma(texCoords, pc.projInfo.xy, pc.projInfo.zw) * (pc.projOrtho != 0 ? 1.0f : eyeZ), eyeZ);
}
float3 fetchViewPos(float2 texCoords)
{
	float depth = texture(hizBuffer, texCoords).r;
	return texCoordsToView(texCoords, depth);
}
float3 minDiff(float3 pos, float3 pr, float3 pl)
{
	float3 v1 = pr - pos; float3 v2 = pos - pl;
	return dot(v1, v1) < dot(v2, v2) ? v1 : v2;
}
float3 reconstructNormal(float2 texCoords, float3 viewPos)
{
	// TODO: do something with black lines at the hard corners.
	float3 pr = fetchViewPos(texCoords + float2( pc.invFullRes.x, 0.0f));
	float3 pl = fetchViewPos(texCoords + float2(-pc.invFullRes.x, 0.0f));
	float3 pt = fetchViewPos(texCoords + float2(0.0f,  pc.invFullRes.y));
	float3 pb = fetchViewPos(texCoords + float2(0.0f, -pc.invFullRes.y));
	return normalize(cross(minDiff(viewPos, pr, pl), minDiff(viewPos, pt, pb)));
}
float2 rotateDirection(float2 dir, float2 cosSin)
{
	return float2(dir.x * cosSin.x - dir.y * cosSin.y, dir.x * cosSin.y + dir.y * cosSin.x);
}
float falloff(float distanceSq)
{
	return fma(distanceSq, pc.negInvR2, 1.0f);
}

float computeAO(float3 p, float3 n, float3 s)
{
	float3 v = s - p; float vov = dot(v, v); float nov = dot(n, v) * inversesqrt(vov);
	return clamp(nov - pc.novBias, 0.0f, 1.0f) * clamp(falloff(vov), 0.0f, 1.0f);
}
float computeCoarseAO(float2 fullResUV, float radiusP, float3 rand, float3 viewPos, float3 viewNormal)
{
	float stepSizeP = radiusP / (STEP_COUNT + 1);
	const float alpha = (M_PI * 2.0f) / SSAO_DIRECTION_COUNT;

	float ao = 0;
	for (uint32 dirIndex = 0; dirIndex < SSAO_DIRECTION_COUNT; dirIndex++)
	{
		float angle = alpha * dirIndex;
		float2 direction = rotateDirection(float2(cos(angle), sin(angle)), rand.xy);
		float rayP = fma(rand.z, stepSizeP, 1.0f);

		for (float stepIndex = 0; stepIndex < STEP_COUNT; stepIndex++)
		{
			float2 snappedUV = fma(round(rayP * direction), pc.invFullRes, fullResUV);
			float3 s = fetchViewPos(snappedUV);
			ao += computeAO(viewPos, viewNormal, s);
			rayP += stepSizeP;
		}
	}

	ao *= pc.aoMultiplier * (1.0f / (SSAO_DIRECTION_COUNT * STEP_COUNT));
	return clamp(1.0f - (ao * 2.0f), 0.0f, 1.0f);
}

//**********************************************************************************************************************
void main()
{
	float3 viewPos = fetchViewPos(fs.texCoords);
	float3 viewNormal = reconstructNormal(fs.texCoords, viewPos);
	float radiusP = pc.radiusToScreen / (pc.projOrtho != 0 ? 1.0f : viewPos.z);
	float3 rand = texture(noise, gl.fragCoord.xy / SSAO_NOISE_SIZE).xyz;
	float ao = computeCoarseAO(fs.texCoords, radiusP, rand, viewPos, viewNormal);
	fb.ao = float4(pow(ao, pc.powExponent));
}
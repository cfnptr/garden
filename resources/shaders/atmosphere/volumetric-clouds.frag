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

// Physically based volumetric clouds.
// Based on this: https://www.researchgate.net/publication/345694869_Physically_Based_Sky_Atmosphere_Cloud_Rendering_in_Frostbite

#include "common/depth.gsl"
#include "common/random.gsl"
#include "atmosphere/common.gsl"

spec const float SAMPLE_COUNT = 16.0f;

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler3D
{
	filter = linear;
	addressMode = repeat;
} noiseShape;
uniform sampler3D
{
	filter = linear;
	addressMode = repeat;
} noiseErosion;

uniform pushConstants
{
	float4x4 invViewProj;
	float3 cameraPos;
	float bottomRadius;
	float topRadius;
	float minDistance;
	float maxDistance;
} pc;

//**********************************************************************************************************************
void main()
{
	float3 viewDirection = calcViewDirection(fs.texCoords, pc.invViewProj);
	Ray ray = Ray(pc.cameraPos, viewDirection);
	float2 tTop = raycast2(Sphere(float3(0.0f), pc.topRadius), ray);
	if (!isIntersected(tTop))
	{
		fb.color = float4(0.0f);
		return;
	}

	float2 tBottom = raycast2(Sphere(float3(0.0f), pc.bottomRadius), ray);
	float tMin; float tMax;
	if (isIntersected(tBottom))
	{
		float bottom = all(greaterThan(tBottom, float2(0.0f))) ? 
			min(tBottom.x, tBottom.y) : max(tBottom.x, tBottom.y);
		float top = all(greaterThan(tTop, float2(0.0f))) ? 
			min(tTop.x, tTop.y) : max(tTop.x, tTop.y);
		if (all(greaterThan(tBottom, float2(0.0f))))
			top = max(min(tTop.x, tTop.y), 0.0f);
		tMin = min(bottom, top); tMax = max(bottom, top);
	}
	else
	{
		tMin = tTop.x; tMax = tTop.y;
	}
	tMin = max(tMin, 0.0f); tMax = max(tMax, 0.0f);

	if (tMax <= tMin || tMin > pc.maxDistance)
	{
		fb.color = float4(0.0f);
		return;
	}

	// TODO: sample depth buffer and update tMax.

	tMin = max(tMin, pc.minDistance); tMax = max(tMax, tMin);
	float t = tMin + SAMPLE_COUNT * 0.5f;
	float3 color = float3(0.0f);

	for (; t < tMax; t += SAMPLE_COUNT)
	{
		float3 samplePos = fma(viewDirection, float3(t), pc.cameraPos);
		float noiseR = textureLod(noiseShape, samplePos * 0.05f, 0.0f).r;
		float noiseG = textureLod(noiseShape, samplePos * 0.1f, 0.0f).g;
		float noiseB = textureLod(noiseShape, samplePos * 0.5f, 0.0f).b;
		float noiseA = textureLod(noiseShape, samplePos * 1.f, 0.0f).a;
		// TODO:
		color += noiseR * noiseG * noiseB * noiseA * 0.1f;
	}
	
	fb.color = float4(float3(1.0f), color.r * ((tMax - tMin) * (1.0f / SAMPLE_COUNT)));
}
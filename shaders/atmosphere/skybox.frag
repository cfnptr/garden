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

// Physically based atmosphere skybox and camera view.

#define USE_CAMERA_VOLUME

spec const bool USE_CUBEMAP_ONLY = false;
spec const float SLICE_COUNT = 8.0f;
spec const float KM_PER_SLICE = 12.0f;

#include "common/depth.gsl"
#include "atmosphere/common.gsl"

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D depthBuffer;

uniform sampler2D
{
	filter = linear;
} transLUT;
uniform sampler2D
{
	filter = linear;
} skyViewLUT;
uniform sampler3D
{
	filter = linear;
} cameraVolume;

uniform pushConstants
{
	float4x4 invViewProj;
	float3 cameraPos;
	float bottomRadius;
	float3 starDir;
	float topRadius;
	float3 starColor;
	float starSize;
} pc;

//**********************************************************************************************************************
float3 getStarLuminance(float3 worldDir, bool intersectGround)
{
	// Note: No early exit to smooth the star disk.
	if (intersectGround)
		return float3(0.0f);
	float3 transmittance = getTransmittance(transLUT, Ray(pc.cameraPos, worldDir), pc.bottomRadius, pc.topRadius);
	float starDisk = saturate(((dot(worldDir, pc.starDir) - pc.starSize) * 2.0f) / (1.0f - pc.starSize));
	return transmittance * pc.starColor * starDisk;
}

void main()
{
	float depth = USE_CUBEMAP_ONLY ? FAR_PLANE_DEPTH : textureLod(depthBuffer, fs.texCoords, 0.0f).x;
	float3 worldDir = calcViewDirection(fs.texCoords, pc.invViewProj); float viewHeight = length(pc.cameraPos);

	if (viewHeight < pc.topRadius && depth == FAR_PLANE_DEPTH)
	{
		bool intersectGround = raycast(Sphere(float3(0.0f), pc.bottomRadius), Ray(pc.cameraPos, worldDir));
		float2 uv = skyViewToUV(skyViewLUT, pc.cameraPos, pc.bottomRadius, 
			pc.starDir, viewHeight, intersectGround, worldDir);
		float3 skyColor = textureLod(skyViewLUT, uv, 0.0f).rgb + getStarLuminance(worldDir, intersectGround);
		fb.color = float4(min(skyColor, float3(FLOAT_BIG_16)), 1.0f);
		return;
	}

	if (USE_CUBEMAP_ONLY)
		return;

	float3 worldPos = calcWorldPosition(depth, fs.texCoords, pc.invViewProj);
	fb.color = getAerialPerspLuminance(cameraVolume, fs.texCoords, length(worldPos * 0.001f));
}
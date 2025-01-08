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

#include "common/depth.gsl"
#include "common/gbuffer.gsl"

#define SHADOW_MAP_CASCADE_COUNT 3

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
	wrap = clampToBorder;
} shadowMap;

uniform DataBuffer
{
	float4x4 lightSpace[SHADOW_MAP_CASCADE_COUNT];
} data;

uniform pushConstants
{
	float4 farNearPlanes;
	float4 lightDir;
	float minBias;
	float maxBias;
	float intensity;
} pc;

//**********************************************************************************************************************
void main()
{
	float pixelDepth = texture(depthBuffer, fs.texCoords).r;
	if (pixelDepth < pc.farNearPlanes.z)
		discard;

	float2 steps = step(pc.farNearPlanes.xy, float2(pixelDepth));
	uint32 cascadeID = (SHADOW_MAP_CASCADE_COUNT - 1) - uint32(steps.x + steps.y);
	float4 lightProj = data.lightSpace[cascadeID] * float4(fs.texCoords, pixelDepth, 1.0f);
	float3 lightCoords = lightProj.xyz / lightProj.w;
	if (lightCoords.z < 0.0f)
		discard;

	float3 normal = decodeNormal(texture(gBufferNormals, fs.texCoords));
	float bias = max((dot(normal, pc.lightDir.xyz) + 1.0f) * pc.maxBias, pc.minBias);
	bias /= (pc.farNearPlanes.w / pc.farNearPlanes[cascadeID]) * 0.5f;
	lightCoords.z += bias;

	float2 texelSize = 1.0f / textureSize(shadowMap, 0).xy;
	float shadow = 0.0f;

	for (int32 y = -1; y <= 1; y++)
	{
		for (int32 x = -1; x <= 1; x++)
		{
			shadow += texture(shadowMap, float4(float2(x, y) *
				texelSize + lightCoords.xy, cascadeID, lightCoords.z));
		}
	}

	if (shadow == 0.0f)
		discard;

	shadow *= (1.0f / 9.0f);
	fb.shadow = float4(1.0f - shadow * pc.intensity, 0.0f, 0.0f, 0.0f);
}
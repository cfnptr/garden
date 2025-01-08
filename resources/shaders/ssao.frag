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

// Based on this: https://lettier.github.io/3d-game-shaders-for-beginners/ssao.html

#include "common/gbuffer.gsl"
#include "common/constants.gsl"

#define NOISE_SIZE 4
spec const uint32 SAMPLE_COUNT = 32;

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.ao;

uniform sampler2D gBufferNormals;
uniform sampler2D depthBuffer;

uniform sampler2D
{
	wrap = repeat;
} noise;
uniform SampleBuffer
{
	float4 data[SAMPLE_COUNT];
} samples;
uniform CameraConstants 
{
	CAMERA_CONSTANTS
} cc;

uniform pushConstants
{
	float4x4 uvToView;
	float4x4 viewToUv;
} pc;

//**********************************************************************************************************************
void main()
{
	float depth = texture(depthBuffer, fs.texCoords).x;
	if (depth < FLOAT_EPS6)
		discard;

	float4x4 uvToView = pc.uvToView;
	float radius = uvToView[0][3], bias = uvToView[1][3], intensity = uvToView[3][3];
	uvToView[0][3] = 0.0f; uvToView[1][3] = 0.0f, uvToView[3][3] = 0.0f;

	float4 viewPos = uvToView * float4(fs.texCoords, depth, 1.0f);
	viewPos.xyz /= viewPos.w;
	float3 normal = decodeNormal(texture(gBufferNormals, fs.texCoords));
	normal = normalize(float3x3(cc.view) * normal);

	float2 noiseScale = textureSize(gBufferNormals, 0) * (1.0f / NOISE_SIZE);
	float3 random = texture(noise, fs.texCoords * noiseScale).xyz;

	float3 tangent = normalize(random - normal * dot(random, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 tbn = float3x3(tangent, bitangent, normal);

	float occlusion = 0.0;
	for (int32 i = 0; i < SAMPLE_COUNT; i++)
	{
		float3 position = tbn * samples.data[i].xyz * radius + viewPos.xyz;
		float4 offset = pc.viewToUv * float4(position, 1.0);
		offset.xy /= offset.w;
		float sampleDepth = texture(depthBuffer, offset.xy).x;
		float4 samplePos = uvToView * float4(offset.xy, sampleDepth, 1.0f);
		samplePos.z /= samplePos.w;
		float rangeCheck = smoothstep(0.0f, 1.0f, radius / abs(viewPos.z - samplePos.z));
		occlusion += (samplePos.z <= position.z + bias ? 1.0f : 0.0f) * rangeCheck;
	}

	float ao = occlusion * (1.0f / SAMPLE_COUNT) * intensity;
	fb.ao = float4(1.0f - ao, 0.0f, 0.0f, 0.0f);
}
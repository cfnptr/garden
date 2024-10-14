// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
#include "common/tone-mapping.gsl"

pipelineState
{
	faceCulling = off;
}

#define OFF_DRAW_MODE 0
#define HDR_DRAW_MODE 1
#define BASE_COLOR_DRAW_MODE 2
#define METALLIC_DRAW_MODE 3
#define ROUGHNESS_DRAW_MODE 4
#define REFLECTANCE_DRAW_MODE 5
#define EMISSIVE_DRAW_MODE 6
#define NORMAL_DRAW_MODE 7
#define WORLD_POSITION_DRAW_MODE 8
#define DEPTH_DRAW_MODE 9
#define LIGHTING_DRAW_MODE 10
#define SHADOW_DRAW_MODE 11
#define AMBIENT_OCCLUSION_DRAW_MODE 12
#define AMBIENT_OCCLUSION_D_DRAW_MODE 13
#define DRAW_MODE_COUNT 14

in float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D gBuffer0;
uniform sampler2D gBuffer1;
uniform sampler2D gBuffer2;
uniform sampler2D hdrBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D shadowBuffer0;
uniform sampler2D aoBuffer0;
uniform sampler2D aoBuffer1;

uniform pushConstants
{
	float4x4 viewProjInv;
	int32 drawMode;
	float showChannelR;
	float showChannelG;
	float showChannelB;
} pc;

//**********************************************************************************************************************
void main()
{
	if (pc.drawMode == HDR_DRAW_MODE)
	{
		float3 hdrColor = texture(hdrBuffer, fs.texCoords).rgb;
		fb.color = float4(gammaCorrection(hdrColor), 1.0f);
	}
	else if (pc.drawMode == BASE_COLOR_DRAW_MODE)
	{
		float3 baseColor = decodeBaseColor(texture(gBuffer0, fs.texCoords));
		fb.color = float4(baseColor, 1.0f);
	}
	else if (pc.drawMode == METALLIC_DRAW_MODE)
	{
		float metallic = decodeMetallic(texture(gBuffer0, fs.texCoords));
		fb.color = float4(float3(metallic), 1.0f);
	}
	else if (pc.drawMode == ROUGHNESS_DRAW_MODE)
	{
		float roughness = decodeRoughness(texture(gBuffer2, fs.texCoords));
		fb.color = float4(float3(roughness), 1.0f);
	}
	else if (pc.drawMode == REFLECTANCE_DRAW_MODE)
	{
		float reflectance = decodeReflectance(texture(gBuffer1, fs.texCoords));
		fb.color = float4(float3(reflectance), 1.0f);
	}
	else if (pc.drawMode == EMISSIVE_DRAW_MODE)
	{
		float3 emissive = decodeEmissive(texture(gBuffer2, fs.texCoords));
		fb.color = float4(emissive, 1.0f);
	}
	else if (pc.drawMode == NORMAL_DRAW_MODE)
	{
		float3 normal = decodeNormal(texture(gBuffer1, fs.texCoords));
		fb.color = float4(normal * 0.5f + 0.5f, 1.0f);
	}
	else if (pc.drawMode == WORLD_POSITION_DRAW_MODE)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		float3 worldPos = calcWorldPosition(depth, fs.texCoords, pc.viewProjInv);
		fb.color = float4(log(abs(worldPos) + float3(1.0f)) * 0.1f, 1.0f);
	}
	else if (pc.drawMode == DEPTH_DRAW_MODE)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		fb.color = float4(float3(pow(depth, (1.0f / 2.0f))), 1.0f);
	}
	else if (pc.drawMode == SHADOW_DRAW_MODE)
	{
		float shadow = texture(shadowBuffer0, fs.texCoords).r;
		fb.color = float4(shadow, shadow, shadow, 1.0f);
	}
	else if (pc.drawMode == AMBIENT_OCCLUSION_DRAW_MODE)
	{
		float ao = texture(aoBuffer0, fs.texCoords).r;
		fb.color = float4(ao, ao, ao, 1.0f);
	}
	else if (pc.drawMode == AMBIENT_OCCLUSION_D_DRAW_MODE)
	{
		float ao = texture(aoBuffer1, fs.texCoords).r;
		fb.color = float4(ao, ao, ao, 1.0f);
	}
	else
	{
		fb.color = float4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	fb.color.r *= pc.showChannelR;
	fb.color.g *= pc.showChannelG;
	fb.color.b *= pc.showChannelB;
}
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

#define USE_EMISSIVE_BUFFER true
#define USE_GI_BUFFER true

#include "common/depth.gsl"
#include "common/gbuffer.gsl"
#include "common/tone-mapping.gsl"

pipelineState
{
	faceCulling = off;
}

#define OFF_DRAW_MODE 0
#define BASE_COLOR_DRAW_MODE 1
#define SPECULAR_FACTOR_DRAW_MODE 2
#define TRANSMISSION_DRAW_MODE 3
#define METALLIC_DRAW_MODE 4
#define ROUGHNESS_DRAW_MODE 5
#define MATERIAL_AO_DRAW_MODE 6
#define REFLECTANCE_DRAW_MODE 7
#define CLEAR_COAT_ROUGHNESS_DRAW_MODE 8
#define NORMALS_DRAW_MODE 9
#define MATERIAL_SHADOWS_DRAW_MODE 10
#define EMISSIVE_COLOR_DRAW_MODE 11
#define EMISSIVE_FACTOR_DRAW_MODE 12
#define GI_COLOR_DRAW_MODE 13
#define THICKNESS_DRAW_MODE 14
#define LIGHTING_DRAW_MODE 15
#define HDR_DRAW_MODE 16
#define OIT_ACCUM_COLOR_DRAW_MODE 17
#define OIT_ACCUM_ALPHA_DRAW_MODE 18
#define OIT_REVEAL_DRAW_MODE 19
#define DEPTH_DRAW_MODE 20
#define WORLD_POSITION_DRAW_MODE 21
#define GLOBAL_SHADOW_COLOR_DRAW_MODE 22
#define GLOBAL_SHADOW_ALPHA_DRAW_MODE 23
#define GLOBAL_AO_DRAW_MODE 24
#define DENOISED_GLOBAL_AO_DRAW_MODE 25
#define DRAW_MODE_COUNT 26

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D g0;
uniform sampler2D g1;
uniform sampler2D g2;
uniform sampler2D g3;
uniform sampler2D g4;
uniform sampler2D g5;

uniform sampler2D hdrBuffer;
uniform sampler2D oitAccumBuffer;
uniform sampler2D oitRevealBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D shadowBuffer0;
uniform sampler2D aoBuffer0;
uniform sampler2D aoBuffer1;

uniform pushConstants
{
	float4x4 invViewProj;
	int32 drawMode;
	float showChannelR;
	float showChannelG;
	float showChannelB;
} pc;

//**********************************************************************************************************************
void main()
{
	const GBufferValues gBuffer = DECODE_G_BUFFER_VALUES(fs.texCoords);

	if (pc.drawMode == OFF_DRAW_MODE)
	{
		discard;
	}
	else if (pc.drawMode == BASE_COLOR_DRAW_MODE)
	{
		fb.color = float4(gBuffer.baseColor, 1.0f);
	}
	else if (pc.drawMode == SPECULAR_FACTOR_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.specularFactor), 1.0f);
	}
	else if (pc.drawMode == TRANSMISSION_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.transmission), 1.0f);
	}
	else if (pc.drawMode == METALLIC_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.metallic), 1.0f);
	}
	else if (pc.drawMode == ROUGHNESS_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.roughness), 1.0f);
	}
	else if (pc.drawMode == MATERIAL_AO_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.ambientOcclusion), 1.0f);
	}
	else if (pc.drawMode == REFLECTANCE_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.reflectance), 1.0f);
	}
	else if (pc.drawMode == CLEAR_COAT_ROUGHNESS_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.clearCoatRoughness), 1.0f);
	}
	else if (pc.drawMode == NORMALS_DRAW_MODE)
	{
		float3 normal = gBuffer.normal * 0.5f + 0.5f;
		fb.color = float4(gammaCorrection(normal, DEFAULT_GAMMA), 1.0f);
	}
	else if (pc.drawMode == MATERIAL_SHADOWS_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.shadow), 1.0f);
	}
	else if (pc.drawMode == EMISSIVE_COLOR_DRAW_MODE)
	{
		fb.color = float4(gBuffer.emissiveColor, 1.0f);
	}
	else if (pc.drawMode == EMISSIVE_FACTOR_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.emissiveFactor), 1.0f);
	}
	else if (pc.drawMode == GI_COLOR_DRAW_MODE)
	{
		fb.color = float4(gBuffer.giColor, 1.0f);
	}
	else if (pc.drawMode == THICKNESS_DRAW_MODE)
	{
		fb.color = float4(float3(gBuffer.thickness), 1.0f);
	}
	else if (pc.drawMode == HDR_DRAW_MODE)
	{
		float3 hdrColor = texture(hdrBuffer, fs.texCoords).rgb;
		fb.color = float4(gammaCorrection(hdrColor), 1.0f);
	}
	else if (pc.drawMode == OIT_ACCUM_COLOR_DRAW_MODE)
	{
		float3 oitColor = texture(oitAccumBuffer, fs.texCoords).rgb;
		fb.color = float4(gammaCorrection(oitColor), 1.0f);
	}
	else if (pc.drawMode == OIT_ACCUM_ALPHA_DRAW_MODE)
	{
		float oitAlpha = texture(oitAccumBuffer, fs.texCoords).a;
		fb.color = float4(float3(oitAlpha), 1.0f);
	}
	else if (pc.drawMode == OIT_REVEAL_DRAW_MODE)
	{
		float oitReveal = texture(oitRevealBuffer, fs.texCoords).r;
		fb.color = float4(float3(oitReveal), 1.0f);
	}
	else if (pc.drawMode == DEPTH_DRAW_MODE)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		fb.color = float4(float3(pow(depth, (1.0f / 2.0f))), 1.0f);
	}
	else if (pc.drawMode == WORLD_POSITION_DRAW_MODE)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		float3 worldPos = calcWorldPosition(depth, fs.texCoords, pc.invViewProj);
		fb.color = float4(log(abs(worldPos) + float3(1.0f)) * 0.1f, 1.0f);
	}
	else if (pc.drawMode == GLOBAL_SHADOW_COLOR_DRAW_MODE)
	{
		float3 shadowColor = texture(shadowBuffer0, fs.texCoords).rgb;
		fb.color = float4(shadowColor, 1.0f);
	}
	else if (pc.drawMode == GLOBAL_SHADOW_ALPHA_DRAW_MODE)
	{
		float shadowAlpha = texture(shadowBuffer0, fs.texCoords).a;
		fb.color = float4(float3(shadowAlpha), 1.0f);
	}
	else if (pc.drawMode == GLOBAL_AO_DRAW_MODE)
	{
		float ao = texture(aoBuffer0, fs.texCoords).r;
		fb.color = float4(float3(ao), 1.0f);
	}
	else if (pc.drawMode == DENOISED_GLOBAL_AO_DRAW_MODE)
	{
		float ao = texture(aoBuffer1, fs.texCoords).r;
		fb.color = float4(float3(ao), 1.0f);
	}
	else
	{
		fb.color = float4(1.0f, 0.0f, 1.0f, 1.0f);
	}

	fb.color.r *= pc.showChannelR;
	fb.color.g *= pc.showChannelG;
	fb.color.b *= pc.showChannelB;
}
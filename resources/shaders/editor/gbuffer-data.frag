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
#include "editor/gbuffer-data.h"

pipelineState
{
	faceCulling = off;
}

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
uniform sampler2D shadowBuffer;
uniform sampler2D shadowDenoisedBuffer;
uniform sampler2D aoBuffer;
uniform sampler2D aoDenoisedBuffer;

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

	if (pc.drawMode == G_BUFFER_DRAW_MODE_OFF)
	{
		discard;
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_BASE_COLOR)
	{
		fb.color = float4(gBuffer.baseColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_SPECULAR_FACTOR)
	{
		fb.color = float4(float3(gBuffer.specularFactor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_TRANSMISSION)
	{
		fb.color = float4(float3(gBuffer.transmission), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_METALLIC)
	{
		fb.color = float4(float3(gBuffer.metallic), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_ROUGHNESS)
	{
		fb.color = float4(float3(gBuffer.roughness), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_MATERIAL_AO)
	{
		fb.color = float4(float3(gBuffer.ambientOcclusion), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_REFLECTANCE)
	{
		fb.color = float4(float3(gBuffer.reflectance), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_CC_ROUGHNESS)
	{
		fb.color = float4(float3(gBuffer.clearCoatRoughness), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_NORMALS)
	{
		float3 normal = gBuffer.normal * 0.5f + 0.5f;
		fb.color = float4(gammaCorrection(normal, DEFAULT_GAMMA), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_MATERIAL_SHADOWS)
	{
		fb.color = float4(float3(gBuffer.shadow), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_COLOR)
	{
		fb.color = float4(gBuffer.emissiveColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR)
	{
		fb.color = float4(float3(gBuffer.emissiveFactor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GI_COLOR)
	{
		fb.color = float4(gammaCorrection(gBuffer.giColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_HDR_BUFFER)
	{
		float3 hdrColor = texture(hdrBuffer, fs.texCoords).rgb;
		fb.color = float4(gammaCorrection(hdrColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_ACCUM_COLOR)
	{
		float3 oitColor = texture(oitAccumBuffer, fs.texCoords).rgb;
		fb.color = float4(gammaCorrection(oitColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_ACCUM_ALPHA)
	{
		float oitAlpha = texture(oitAccumBuffer, fs.texCoords).a;
		fb.color = float4(float3(oitAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_REVEAL)
	{
		float oitReveal = texture(oitRevealBuffer, fs.texCoords).r;
		fb.color = float4(float3(oitReveal), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_DEPTH_BUFFER)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		fb.color = float4(float3(pow(depth, (1.0f / 2.0f))), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_WORLD_POSITION)
	{
		float depth = texture(depthBuffer, fs.texCoords).r;
		float3 worldPos = calcWorldPosition(depth, fs.texCoords, pc.invViewProj);
		fb.color = float4(log(abs(worldPos) + float3(1.0f)) * 0.1f, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_COLOR)
	{
		float3 shadowColor = texture(shadowBuffer, fs.texCoords).rgb;
		fb.color = float4(shadowColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_ALPHA)
	{
		float shadowAlpha = texture(shadowBuffer, fs.texCoords).a;
		fb.color = float4(float3(shadowAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_D_SHADOW_COLOR)
	{
		float3 shadowColor = texture(shadowDenoisedBuffer, fs.texCoords).rgb;
		fb.color = float4(shadowColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_D_SHADOW_ALPHA)
	{
		float shadowAlpha = texture(shadowDenoisedBuffer, fs.texCoords).a;
		fb.color = float4(float3(shadowAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_AO)
	{
		float ao = texture(aoBuffer, fs.texCoords).r;
		fb.color = float4(float3(ao), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_D_AO)
	{
		float ao = texture(aoDenoisedBuffer, fs.texCoords).r;
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
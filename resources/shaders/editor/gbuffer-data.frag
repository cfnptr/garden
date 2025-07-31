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

#define USE_SPECULAR_FACTOR
#define USE_AMBIENT_OCCLUSION
#define USE_LIGHT_SHADOW
#define USE_CLEAR_COAT
#define USE_LIGHT_EMISSION

#define USE_CLEAR_COAT_BUFFER true
#define USE_EMISSION_BUFFER true

#include "common/depth.gsl"
#include "common/gbuffer.gsl"
#include "common/tone-mapping.gsl"
#include "common/normal-mapping.gsl"
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

uniform sampler2D hdrBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D reflBuffer;
uniform sampler2D shadowBuffer;
uniform sampler2D shadowBlurBuffer;
uniform sampler2D aoBuffer;
uniform sampler2D aoBlurBuffer;
uniform sampler2D giBuffer;
uniform sampler2D oitAccumBuffer;
uniform sampler2D oitRevealBuffer;

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
		discard;

	if (pc.drawMode == G_BUFFER_DRAW_MODE_HDR_BUFFER)
	{
		float3 hdrColor = textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(gammaCorrection(hdrColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_DEPTH_BUFFER)
	{
		float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).r;
		fb.color = float4(float3(pow(depth, (1.0f / 2.0f))), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_NORMAL_BUFFER)
	{
		fb.color = float4(gammaCorrection(packNormal(gBuffer.normal), DEFAULT_GAMMA), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_REFLECTION_BUFFER)
	{
		float3 reflection = textureLod(reflBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(reflection, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_COLOR)
	{
		float3 shadowColor = textureLod(shadowBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(shadowColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_ALPHA)
	{
		float shadowAlpha = textureLod(shadowBuffer, fs.texCoords, 0.0f).a;
		fb.color = float4(float3(shadowAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_AO)
	{
		float ao = textureLod(aoBuffer, fs.texCoords, 0.0f).r;
		fb.color = float4(float3(ao), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GI_BUFFER)
	{
		float3 giColor = textureLod(giBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(gammaCorrection(giColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_ACCUM_COLOR)
	{
		float3 oitColor = textureLod(oitAccumBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(gammaCorrection(oitColor), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_ACCUM_ALPHA)
	{
		float oitAlpha = textureLod(oitAccumBuffer, fs.texCoords, 0.0f).a;
		fb.color = float4(float3(oitAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_OIT_REVEAL)
	{
		float oitReveal = textureLod(oitRevealBuffer, fs.texCoords, 0.0f).r;
		fb.color = float4(float3(oitReveal), 1.0f);
	}

	else if (pc.drawMode == G_BUFFER_DRAW_MODE_BASE_COLOR)
		fb.color = float4(gBuffer.baseColor, 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_SPECULAR_FACTOR)
		fb.color = float4(float3(gBuffer.specularFactor), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_METALLIC)
		fb.color = float4(float3(gBuffer.metallic), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_ROUGHNESS)
		fb.color = float4(float3(gBuffer.roughness), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_MATERIAL_AO)
		fb.color = float4(float3(gBuffer.ambientOcclusion), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_REFLECTANCE)
		fb.color = float4(float3(gBuffer.reflectance), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_MATERIAL_SHADOW)
		fb.color = float4(float3(gBuffer.shadow.a), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_COLOR)
		fb.color = float4(gBuffer.emissiveColor, 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR)
		fb.color = float4(float3(gBuffer.emissiveFactor), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_CC_ROUGHNESS)
		fb.color = float4(float3(gBuffer.clearCoatRoughness), 1.0f);
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_CC_NORMAL)
		fb.color = float4(gammaCorrection(packNormal(gBuffer.clearCoatNormal), DEFAULT_GAMMA), 1.0f);

	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_BLURED_SHADOW_COLOR)
	{
		float3 shadowColor = textureLod(shadowBlurBuffer, fs.texCoords, 0.0f).rgb;
		fb.color = float4(shadowColor, 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_BLURED_SHADOW_ALPHA)
	{
		float shadowAlpha = textureLod(shadowBlurBuffer, fs.texCoords, 0.0f).a;
		fb.color = float4(float3(shadowAlpha), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_GLOBAL_BLURED_AO)
	{
		float ao = textureLod(aoBlurBuffer, fs.texCoords, 0.0f).r;
		fb.color = float4(float3(ao), 1.0f);
	}
	else if (pc.drawMode == G_BUFFER_DRAW_MODE_WORLD_POSITION)
	{
		float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).r;
		float3 worldPosition = calcWorldPosition(depth, fs.texCoords, pc.invViewProj);
		fb.color = float4(log(abs(worldPosition) + float3(1.0f)) * 0.1f, 1.0f);
	}
	else fb.color = float4(1.0f, 0.0f, 1.0f, 1.0f);

	fb.color.r *= pc.showChannelR;
	fb.color.g *= pc.showChannelG;
	fb.color.b *= pc.showChannelB;
}
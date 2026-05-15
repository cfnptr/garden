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

#define USE_SPECULAR_FACTOR
#define USE_AMBIENT_OCCLUSION
#define USE_SHADOW_ALPHA
#define USE_LIGHT_EMISSION
#define USE_MOTION_VECTORS
#define USE_VELOCITY_BUFFER true
#variantCount 27

#include "common/depth.gsl"
#include "common/gbuffer.gsl"
#include "common/tone-mapping.gsl"
#include "editor/gbuffer-data.h"

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D g0;
uniform sampler2D g1;
uniform sampler2D g2;
uniform sampler2D g3;

uniform sampler2D hdrBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D reflBuffer;
uniform sampler2D shadBuffer;
uniform sampler2D shadBlurBuffer;
uniform sampler2D aoBuffer;
uniform sampler2D aoBlurBuffer;
uniform sampler2D giBuffer;
uniform sampler2D oitAccumBuffer;
uniform sampler2D oitRevealBuffer;
uniform sampler2D disocclMap;

uniform pushConstants
{
	float4x4 invViewProj;
	float showChannelR;
	float showChannelG;
	float showChannelB;
} pc;

//**********************************************************************************************************************
void main()
{
	GBufferValues gBuffer = decodeGBufferValues(g0, g1, g2, g3, fs.texCoords);
	fb.color = float4(1.0f, 0.0f, 1.0f, 1.0f);

	if (gsl.variant == G_BUFFER_DRAW_MODE_HDR_BUFFER)
		fb.color = float4(gammaCorrection(textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_DEPTH_BUFFER)
		fb.color = float4(float3(pow(textureLod(depthBuffer, fs.texCoords, 0.0f).x, (1.0f / 2.0f))), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_NORMAL_BUFFER)
		fb.color = float4(gammaCorrection(packNormal(gBuffer.normal), DEFAULT_GAMMA), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_REFLECTION_BUFFER)
		fb.color = float4(textureLod(reflBuffer, fs.texCoords, 0.0f).rgb, 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_SHADOW_COLOR)
		fb.color = float4(textureLod(shadBuffer, fs.texCoords, 0.0f).rgb, 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_SHADOW_ALPHA)
		fb.color = float4(float3(textureLod(shadBuffer, fs.texCoords, 0.0f).a), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_AO_BUFFER)
		fb.color = float4(float3(textureLod(aoBuffer, fs.texCoords, 0.0f).x), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_GI_BUFFER)
		fb.color = float4(gammaCorrection(textureLod(giBuffer, fs.texCoords, 0.0f).rgb), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_OIT_COLOR)
		fb.color = float4(gammaCorrection(textureLod(oitAccumBuffer, fs.texCoords, 0.0f).rgb), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_OIT_ALPHA)
		fb.color = float4(float3(textureLod(oitAccumBuffer, fs.texCoords, 0.0f).a), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_OIT_REVEAL)
		fb.color = float4(float3(textureLod(oitRevealBuffer, fs.texCoords, 0.0f).x), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_BASE_COLOR)
		fb.color = float4(gBuffer.baseColor.rgb, 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_SPECULAR_FACTOR)
		fb.color = float4(float3(gBuffer.specularFactor), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_METALLIC)
		fb.color = float4(float3(gBuffer.metallic), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_ROUGHNESS)
		fb.color = float4(float3(gBuffer.roughness), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_MATERIAL_AO)
		fb.color = float4(float3(gBuffer.ambientOcclusion), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_REFLECTANCE)
		fb.color = float4(float3(gBuffer.reflectance), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_MATERIAL_SHADOW)
		fb.color = float4(float3(gBuffer.shadowAlpha), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_VELOCITY)
		fb.color = float4(abs(gBuffer.velocity), float2(0.0f));
	if (gsl.variant == G_BUFFER_DRAW_MODE_DISOCCLUSION)
		fb.color = float4(float3(textureLod(disocclMap, fs.texCoords, 0.0f).x), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_BLURRED_SHADOW_COLOR)
		fb.color = float4(textureLod(shadBlurBuffer, fs.texCoords, 0.0f).rgb, 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_BLURRED_SHADOW_ALPHA)
		fb.color = float4(float3(textureLod(shadBlurBuffer, fs.texCoords, 0.0f).a), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_BLURRED_AO)
		fb.color = float4(float3(textureLod(aoBlurBuffer, fs.texCoords, 0.0f).x), 1.0f);
	if (gsl.variant == G_BUFFER_DRAW_MODE_HDR_LUMA)
		fb.color = float4(float3(rgbToLuma(textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb)), 1.0f);

	if (gsl.variant == G_BUFFER_DRAW_MODE_WORLD_POSITION)
	{
		float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).x;
		float3 worldPosition = calcWorldPosition(depth, fs.texCoords, pc.invViewProj);
		fb.color = float4(log(abs(worldPosition) + float3(1.0f)) * 0.1f, 1.0f);
	}

	fb.color.r *= pc.showChannelR;
	fb.color.g *= pc.showChannelG;
	fb.color.b *= pc.showChannelB;
}
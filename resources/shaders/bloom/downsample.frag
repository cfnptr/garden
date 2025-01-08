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
#include "common/tone-mapping.gsl"

#variantCount 2
#define DOWNSAMPLE_0_VARIANT 0

spec const bool USE_THRESHOLD = false;
spec const bool USE_ANTI_FLICKERING = true;

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D
{
	filter = linear;
} srcTexture;

uniform pushConstants
{
	float threshold;
} pc;

float3 box4x4(float3 s0, float3 s1, float3 s2, float3 s3)
{
	return (s0 + s1 + s2 + s3) * 0.25f;
}
float3 box4x4Karis(float3 s0, float3 s1, float3 s2, float3 s3)
{
	// Karis's luma weighted average
	float w0 = 1.0f / (1.0f + rgbToLuma(s0));
	float w1 = 1.0f / (1.0f + rgbToLuma(s1));
	float w2 = 1.0f / (1.0f + rgbToLuma(s2));
	float w3 = 1.0f / (1.0f + rgbToLuma(s3));
	return (s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) * (1.0f / (w0 + w1 + w2 + w3));
}

// TODO: we can compute karis average only once for each box4x4.

//**********************************************************************************************************************
float3 downsample(sampler2D srcTexture, float2 texCoords, float threshold, bool useThreshold, bool antiFlickering)
{
	float3 c   = texture(srcTexture, texCoords).rgb;

	float3 lt  = textureOffset(srcTexture, texCoords, int2(-1, -1)).rgb;
	float3 rt  = textureOffset(srcTexture, texCoords, int2( 1, -1)).rgb;
	float3 rb  = textureOffset(srcTexture, texCoords, int2( 1,  1)).rgb;
	float3 lb  = textureOffset(srcTexture, texCoords, int2(-1,  1)).rgb;

	float3 lt2 = textureOffset(srcTexture, texCoords, int2(-2, -2)).rgb;
	float3 rt2 = textureOffset(srcTexture, texCoords, int2( 2, -2)).rgb;
	float3 rb2 = textureOffset(srcTexture, texCoords, int2( 2,  2)).rgb;
	float3 lb2 = textureOffset(srcTexture, texCoords, int2(-2,  2)).rgb;

	float3 l   = textureOffset(srcTexture, texCoords, int2(-2,  0)).rgb;
	float3 t   = textureOffset(srcTexture, texCoords, int2( 0, -2)).rgb;
	float3 r   = textureOffset(srcTexture, texCoords, int2( 2,  0)).rgb;
	float3 b   = textureOffset(srcTexture, texCoords, int2( 0,  2)).rgb;

	float3 c0, c1;
	if (antiFlickering) // Anti fireflies
	{
		c0  = box4x4Karis(lt, rt, rb, lb);
		c1  = box4x4Karis(c,  l,  t,  lt2);
		c1 += box4x4Karis(c,  r,  t,  rt2);
		c1 += box4x4Karis(c,  r,  b,  rb2);
		c1 += box4x4Karis(c,  l,  b,  lb2);
	}
	else
	{
		c0  = box4x4(lt, rt, rb, lb);
		c1  = box4x4(c,  l,  t,  lt2);
		c1 += box4x4(c,  r,  t,  rt2);
		c1 += box4x4(c,  r,  b,  rb2);
		c1 += box4x4(c,  l,  b,  lb2);
	}

	float3 color = c0 * 0.5f + c1 * 0.125f;
	if (useThreshold)
		color = any(lessThan(color, float3(threshold))) ? float3(0.0f) : color;
	return color;
}

void main()
{
	float3 color;

	if (gsl.variant == DOWNSAMPLE_0_VARIANT)
	{
		color = downsample(srcTexture, fs.texCoords,
			pc.threshold, USE_THRESHOLD, USE_ANTI_FLICKERING);
		color = max(color, 0.0001f);
	}
	else
	{
		color = downsample(srcTexture, fs.texCoords, 0.0f, false, false);
	}

	fb.color = float4(color, 0.0f);
}
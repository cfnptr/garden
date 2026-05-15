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

#include "tone-mapping/functions.h"

spec const uint32 TONE_MAPPER = TONE_MAPPER_NONE;
spec const bool USE_BLOOM_BUFFER = true;
spec const bool USE_LIGHT_ABSORPTION = true;

#include "common/depth.gsl"
#include "common/random.gsl"
#include "common/constants.gsl"
#include "common/tone-mapping.gsl"

uniform pushConstants
{
	uint32 frameIndex;
	float exposureFactor;
	float ditherIntensity;
	float bloomIntensity;
	float3 absorptionColor;
} pc;

uniform Luminance
{
	float avgLuminance;
	float exposure;
} luminance;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;

uniform sampler2D hdrBuffer;

uniform sampler2D
{
	filter = linear;
} depthBuffer;
uniform sampler2D
{
	filter = linear;
} bloomBuffer;

in noperspective float2 fs.texCoords;
out float4 fb.ldr;

//**********************************************************************************************************************
void main()
{
	float3 hdrColor = textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb;

	if (USE_BLOOM_BUFFER)	
	{
		float3 bloomColor = min(textureLod(bloomBuffer, fs.texCoords, 0.0f).rgb, 65500.0f); // r11b11b10
		hdrColor = lerp(hdrColor, bloomColor, pc.bloomIntensity);
	}
	if (USE_LIGHT_ABSORPTION && dot(pc.absorptionColor, pc.absorptionColor) > 0.0f)
	{
		float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).x;
		float3 worldPos = calcWorldPosition(depth, fs.texCoords, cc.invViewProj);
		hdrColor *= exp(pc.absorptionColor * length(worldPos));
	}

	// TODO: lens dirt? bloomColor + bloomColor * dirtColor * dirtIntensity

	float3 xyyColor = rgbToXyy(hdrColor);
	xyyColor.z *= luminance.exposure * pc.exposureFactor;
	hdrColor = xyyToRgb(xyyColor);

	if (TONE_MAPPER == TONE_MAPPER_ACES_FAST)
		hdrColor = lottesTonemap(hdrColor * 0.8f);
	else if (TONE_MAPPER == TONE_MAPPER_ACES_FILMIC)
		hdrColor = acesFilmicTonemap(hdrColor * 1.6f);
	else if (TONE_MAPPER == TONE_MAPPER_UCHIMURA)
		hdrColor = uchimuraTonemap(hdrColor);
	else if (TONE_MAPPER == TONE_MAPPER_PBR_NEUTRAL)
		hdrColor = pbrNeutralTonemap(hdrColor);

	float3 ldrColor = rgbToSrgb(hdrColor);
	float random = toFloat01(pcg(uint3(gl.fragCoord.xy, pc.frameIndex)).x);
	ldrColor += lerp(-pc.ditherIntensity, pc.ditherIntensity, random);
	fb.ldr = float4(ldrColor, calcLum(ldrColor));
}
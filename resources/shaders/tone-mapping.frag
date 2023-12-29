//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "common/random.gsl"
#include "common/color-space.gsl"
#include "common/tone-mapping.gsl"

#define ACES_TONE_MAPPER 0
#define UCHIMURA_TONE_MAPPER 1

pipelineState
{
	faceCulling = off;
}

spec const bool USE_BLOOM_BUFFER = true;
spec const uint32 TONE_MAPPER = ACES_TONE_MAPPER;

uniform pushConstants
{
	uint32 frameIndex;
	float exposureCoeff;
	float ditherIntensity;
	float bloomIntensity;
} pc;

uniform Luminance
{
	float avgLuminance;
	float exposure;
} luminance;

uniform sampler2D hdrBuffer;

uniform sampler2D
{
	filter = linear;
	wrap = clampToEdge;
} bloomBuffer;

in float2 fs.texCoords;
out float4 fb.ldr;

//--------------------------------------------------------------------------------------------------
void main()
{
	float3 hdrColor = texture(hdrBuffer, fs.texCoords).rgb;

	if (USE_BLOOM_BUFFER)
	{
		float3 bloomColor = min(texture(bloomBuffer, fs.texCoords).rgb, 65500.0f); // r11b11b10
		hdrColor = mix(hdrColor, bloomColor, pc.bloomIntensity);
	}

	// TODO: lens dirt? bloomColor + bloomColor * dirtColor * dirtIntensity

	float3 yxyColor = rgbToYxy(hdrColor);
	yxyColor.x *= luminance.exposure * pc.exposureCoeff;
	hdrColor = yxyToRgb(yxyColor);

	float3 tonemappedColor;
	if (TONE_MAPPER == ACES_TONE_MAPPER)
	{
		tonemappedColor = aces(hdrColor);
	}
	else
	{
		tonemappedColor = uchimura(hdrColor);
	}

	float3 ldrColor = gammaCorrectionPrecise(tonemappedColor); // TODO: set precise or not via spec const?

	float random = toFloat(pcg(uint3(gl.fragCoord.xy, pc.frameIndex)).x);
	ldrColor += mix(-pc.ditherIntensity, pc.ditherIntensity, random);
	fb.ldr = float4(ldrColor, 1.0f);
}
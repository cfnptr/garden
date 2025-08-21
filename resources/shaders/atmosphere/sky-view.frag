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

// Physically based atmosphere sky view look up table.
// Based on this: https://github.com/sebh/UnrealEngineSkyAtmosphere

#define USE_TRANSMITTANCE_LUT
#define USE_MULTI_SCATTERING_LUT
#define USE_SCAT_VAR_SAMPLE_COUNT
#define USE_SCAT_MIE_RAY_PHASE

#define RAY_MARCH_SPP_MIN 4 // TODO: use spec const?
#define RAY_MARCH_SPP_MAX 14
#define RAY_MARCH_SPP_MUL 0.01f // TODO: (1.0f / 150.0f)

#include "common/depth.gsl"
#include "common/constants.gsl"
#include "atmosphere/common.gsl"

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D
{
	filter = linear;
} transLUT;
uniform sampler2D
{
	filter = linear;
} multiScatLUT;

uniform pushConstants
{
	float3 rayleighScattering;
	float rayDensityExpScale;
	float3 mieExtinction;
	float mieDensityExpScale;
	float3 absorptionExtinction;
	float miePhaseG;
	float3 mieScattering;
	float absDensity0LayerWidth;
	float3 sunDir;
	float absDensity0ConstantTerm;
	float3 cameraPos;
	float absDensity0LinearTerm;
	float2 skyViewLutSize;
	float absDensity1ConstantTerm;
	float absDensity1LinearTerm;
	float bottomRadius;
	float topRadius;
} pc;

AtmosphereParams getAtmosphereParams()
{
	AtmosphereParams params;
	params.rayleighScattering = pc.rayleighScattering;
	params.rayDensityExpScale = pc.rayDensityExpScale;
	params.mieExtinction = pc.mieExtinction;
	params.mieDensityExpScale = pc.mieDensityExpScale;
	params.absorptionExtinction = pc.absorptionExtinction;
	params.absDensity0LayerWidth = pc.absDensity0LayerWidth;
	params.absDensity0ConstantTerm = pc.absDensity0ConstantTerm;
	params.absDensity0LinearTerm = pc.absDensity0LinearTerm;
	params.absDensity1ConstantTerm = pc.absDensity1ConstantTerm;
	params.absDensity1LinearTerm = pc.absDensity1LinearTerm;
	params.bottomRadius = pc.bottomRadius;
	params.topRadius = pc.topRadius;
	params.mieScattering = pc.mieScattering;
	params.miePhaseG = pc.miePhaseG;
	return params;
}

//**********************************************************************************************************************
void uvToSkyView(AtmosphereParams atmosphere, float viewHeight, 
	float2 uv, out float viewZenithCosAngle, out float lightViewCosAngle)
{
	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible).
	uv = (uv - 0.5f / pc.skyViewLutSize) * (pc.skyViewLutSize / (pc.skyViewLutSize - 1.0f));
	float vHorizon = sqrt(viewHeight * viewHeight - atmosphere.bottomRadius * atmosphere.bottomRadius);
	float beta = acosFast4(vHorizon / viewHeight); float zenithHorizonAngle = M_PI - beta;

	if (uv.y < 0.5f)
	{
		float coord = 1.0f - 2.0f * uv.y;
		viewZenithCosAngle = cos(zenithHorizonAngle * (1.0f - (coord * coord)));
	}
	else
	{
		float coord = fma(uv.y, 2.0f, -1.0f);
		viewZenithCosAngle = cos(fma(beta, coord * coord, zenithHorizonAngle));
	}

	lightViewCosAngle = -fma(uv.x * uv.x, 2.0f, -1.0f);
}

void main()
{
	AtmosphereParams atmosphere = getAtmosphereParams();
	float viewHeight = length(pc.cameraPos); float viewZenithCosAngle; float lightViewCosAngle;
	uvToSkyView(atmosphere, viewHeight, fs.texCoords, viewZenithCosAngle, lightViewCosAngle);

	float3 upVector = pc.cameraPos / viewHeight; float sunZenithCosAngle = dot(upVector, pc.sunDir);
	float3 sunDir = normalize(float3(sqrt(1.0f - sunZenithCosAngle * sunZenithCosAngle), sunZenithCosAngle, 0.0f));
	float3 worldPos = float3(0.0f, viewHeight, 0.0f);
	float viewZenithSinAngle = sqrt(1.0f - viewZenithCosAngle * viewZenithCosAngle);
	float3 worldDir = float3(viewZenithSinAngle * lightViewCosAngle, viewZenithCosAngle,
		viewZenithSinAngle * sqrt(1.0f - lightViewCosAngle * lightViewCosAngle));
	if (!moveToTopAtmosphere(worldPos, worldDir, atmosphere.topRadius))
	{
		fb.color = float4(float3(0.0f), 1.0f); 
		return; // Ray is not intersecting the atmosphere.
	}

	const float sampleCountIni = 30.0f;
	ScatteringResult result = integrateScatteredLuminance(fs.texCoords, worldPos, worldDir, 
		sunDir, atmosphere, sampleCountIni, DEFAULT_T_MAX_MAX, transLUT, multiScatLUT);
	fb.color = float4(result.l, 1.0f);
}
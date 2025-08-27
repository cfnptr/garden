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

// Physically based atmosphere transmittance look up table.
// Based on this: https://github.com/sebh/UnrealEngineSkyAtmosphere

#include "atmosphere/common.gsl"

// Can go a low as 10 samples but energy loss starts to be visible.
spec const float SAMPLE_COUNT = 10.0f;

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.trans;

uniform pushConstants
{
	float3 rayleighScattering;
	float rayDensityExpScale;
	float3 mieExtinction;
	float mieDensityExpScale;
	float3 absorptionExtinction;
	float absDensity0LayerWidth;
	float3 sunDir;
	float absDensity0ConstantTerm;
	float absDensity0LinearTerm;
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
	return params;
}

//**********************************************************************************************************************
void uvToTransmittanceRMU(float2 uv, out float r, out float mu)
{
	// Distance to the top atmosphere boundary for a horizontal ray at ground level.
	float h = sqrt(pc.topRadius * pc.topRadius - pc.bottomRadius * pc.bottomRadius);
	float rHor = h * uv.y; // Distance to the horizon, from which we can compute r.
	r = sqrt(rHor * rHor + pc.bottomRadius * pc.bottomRadius);

	// Distance to the top atmosphere boundary for the ray (r, mu), and its minimum and maximum values 
	// over all mu - obtained for (r, 1) and (r, muHorizon) - from which we can recover mu:
	float dMin = pc.topRadius - r; float dMax = rHor + h; float d = fma(uv.x, dMax - dMin, dMin);
	mu = clamp(d == 0.0f ? 1.0f : (h * h - rHor * rHor - d * d) / (2.0f * r * d), -1.0f, 1.0f);
}

void main()
{
	float viewHeight; float viewZenithCosAngle; // Compute camera position from the LUT coords.
	uvToTransmittanceRMU(fs.texCoords, viewHeight, viewZenithCosAngle);
	float3 worldPos = float3(0.0f, viewHeight, 0.0f);
	float3 worldDir = float3(0.0f, viewZenithCosAngle, sqrt(1.0f - viewZenithCosAngle * viewZenithCosAngle));

	AtmosphereParams atmosphere = getAtmosphereParams();
	float3 transmittance = exp(-integrateScatteredLuminance(fs.texCoords, worldPos, 
		worldDir, pc.sunDir, atmosphere, SAMPLE_COUNT, DEFAULT_T_MAX_MAX).opticalDepth);
	fb.trans = float4(transmittance, 1.0f);
}
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

// Based on this: https://github.com/sebh/UnrealEngineSkyAtmosphere

// TODO: spec const int32?
#define RAY_MARCH_MIN_SPP 4
#define RAY_MARCH_MAX_SPP 14

in float2 fs.texCoords;
out float4 fb.color;

void main()
{

}

/*
#include "atmosphere/common.gsl"

pipelineState
{
	faceCulling = off;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform pushConstants
{
	float4 data0;
	float4 data1;
	float4 data2;
	float4 data3;
	float4 data4;
	float4 data5;
	float4 data6;
	float4 sunDir; // .w is unused
} pc;

AtmosphereParameters getAtmosphereParameters()
{
	AtmosphereParameters parameters;
	parameters.absorptionExtinction = data5.xyz;

	// Traslation from Bruneton2017 parameterisation.
	parameters.rayleighDensityExpScale = pc.data0.w;
	parameters.mieDensityExpScale = pc.data1.w;
	parameters.absorptionDensity0LayerWidth = pc.data5.w;
	parameters.absorptionDensity0ConstantTerm = pc.data6.x;
	parameters.absorptionDensity0LinearTerm = pc.data6.y;
	parameters.absorptionDensity1ConstantTerm = pc.data6.z;
	parameters.absorptionDensity1LinearTerm = pc.data6.w;

	parameters.miePhaseG = pc.data2.w;
	parameters.rayleighScattering = pc.data0.xyz;
	parameters.mieScattering = pc.data1.xyz;
	parameters.mieAbsorption = pc.data3.xyz;
	parameters.mieExtinction = pc.data2.xyz;
	parameters.groundAlbedo = pc.data4.xyz;
	parameters.bottomRadius = pc.data3.w;
	parameters.topRadius = pc.data4.w;
	return parameters;
}

//**********************************************************************************************************************
void main()
{
	AtmosphereParameters atmosphere = getAtmosphereParameters();

	// Compute camera position from LUT coords
	float viewHeight, viewZenithCosAngle;
	uvToLutTransmittanceParams(atmosphere, viewHeight, viewZenithCosAngle, fs.texCoords);

	// A few extra needed constants
	float3 worldPos = float3(0.0f, 0.0f, viewHeight);
	float3 worldDir = float3(0.0f, sqrt(1.0f -
		viewZenithCosAngle * viewZenithCosAngle), viewZenithCosAngle);

	const bool ground = false;
	// Can go a low as 10 sample but energy lost starts to be visible.
	const float sampleCountIni = 40.0f;	
	const float depthBufferValue = -1.0f;
	const bool variableSampleCount = false;
	const bool mieRayPhase = false;

	float3 transmittance = exp(-integrateScatteredLuminance(fs.texCoords,
		worldPos, worldDir, pc.sunDir.xyz, atmosphere, ground, sampleCountIni,
		depthBufferValue, variableSampleCount, mieRayPhase).opticalDepth);

	// Optical depth to transmittance
	return float4(transmittance, 1.0f);
}*/
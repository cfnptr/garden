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

// Physically based atmosphere skybox.
// Based on this: https://github.com/sebh/UnrealEngineSkyAtmosphere

#include "atmosphere/common.gsl"
#include "common/depth.gsl"

spec const bool USE_CUBEMAP_ONLY = false;

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
} skyViewLUT;
uniform sampler2D depthBuffer;

uniform pushConstants
{
	float4x4 invViewProj;
	float3 cameraPos;
	float bottomRadius;
	float3 sunDir;
	float topRadius;
	float3 sunColor;
	float sunSize;
} pc;

//**********************************************************************************************************************
float2 skyViewToUv(bool intersectGround, float viewZenithCosAngle, float lightViewCosAngle, float viewHeight)
{
	float vHorizon = sqrt(viewHeight * viewHeight - pc.bottomRadius * pc.bottomRadius);
	float beta = acosFast4(vHorizon / viewHeight); float zenithHorizonAngle = M_PI - beta;
	float2 uv; uv.x = sqrt(fma(-lightViewCosAngle, 0.5f, 0.5f));

	if (!intersectGround)
	{
		float coord = acosFast4(viewZenithCosAngle) / zenithHorizonAngle;
		uv.y = (1.0f - sqrt(1.0f - coord)) * 0.5f;
	}
	else
	{
		float coord = (acosFast4(viewZenithCosAngle) - zenithHorizonAngle) / beta;
		uv.y = fma(sqrt(coord), 0.5f, 0.5f);
	}

	// Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible).
	const float2 skyViewSize = float2(textureSize(skyViewLUT, 0));
	return (uv + 0.5f / skyViewSize) * (skyViewSize / (skyViewSize + 1.0f));
}

float3 getSunLuminance(float3 worldDir)
{
	// Note: No early exit to smooth the sun disk.
	if (intersectSphere(pc.cameraPos, worldDir, float3(0.0f), pc.bottomRadius) >= 0.0f)
		return float3(0.0f);
	float3 transmittance = getTransmittance(transLUT, pc.cameraPos, worldDir, pc.bottomRadius, pc.topRadius);
	float sunDisk = clamp(((dot(worldDir, pc.sunDir) - pc.sunSize) * 2.0f) / (1.0f - pc.sunSize), 0.0f, 1.0f);
	return transmittance * pc.sunColor * sunDisk;
}

void main()
{
	float depth = USE_CUBEMAP_ONLY ? FAR_PLANE_DEPTH : textureLod(depthBuffer, fs.texCoords, 0.0f).r;
	float3 worldDir = calcViewDirection(0.5f, fs.texCoords, pc.invViewProj);
	float viewHeight = length(pc.cameraPos);

	if (viewHeight < pc.topRadius && depth == FAR_PLANE_DEPTH)
	{
		float3 upVector = normalize(pc.cameraPos); float viewZenithCosAngle = dot(worldDir, upVector);
		float3 sideVector = normalize(cross(upVector, worldDir));
		// Aligns toward the sun light but perpendicular to up vector.
		float3 forwardVector = normalize(cross(sideVector, upVector));
		float2 lightOnPlane = normalize(float2(dot(pc.sunDir, forwardVector), dot(pc.sunDir, sideVector)));
		float lightViewCosAngle = lightOnPlane.x;

		bool intersectGround = intersectSphere(pc.cameraPos, worldDir, float3(0.0f), pc.bottomRadius) >= 0.0f;
		float2 uv = skyViewToUv(intersectGround, viewZenithCosAngle, lightViewCosAngle, viewHeight);
		float3 skyColor = textureLod(skyViewLUT, uv, 0.0f).rgb + getSunLuminance(worldDir);
		fb.color = float4(min(skyColor, float3(FLOAT_BIG_16)), 1.0f);
		return;
	}

	if (USE_CUBEMAP_ONLY)
		return;

	discard;
	float3 l = float3(0.0f);

	// TODO: use 2 variant one with camera volume and second without (for skybox).

	/*
	ClipSpace = float3((pixPos / float2(gResolution))*float2(2.0, -2.0) - float2(1.0, -1.0), DepthBufferValue);
	float4 DepthBufferWorldPos = mul(gSkyInvViewProjMat, float4(ClipSpace, 1.0));
	DepthBufferWorldPos /= DepthBufferWorldPos.w;
	float tDepth = length(DepthBufferWorldPos.xyz - (WorldPos + float3(0.0, 0.0, -Atmosphere.BottomRadius)));
	float Slice = AerialPerspectiveDepthToSlice(tDepth);
	float Weight = 1.0;
	if (Slice < 0.5)
	{
		// We multiply by weight to fade to 0 at depth 0. That works for luminance and opacity.
		Weight = saturate(Slice * 2.0);
		Slice = 0.5;
	}
	float w = sqrt(Slice / AP_SLICE_COUNT);	// squared distribution

	const float4 AP = Weight * AtmosphereCameraScatteringVolume.SampleLevel(samplerLinearClamp, float3(pixPos / float2(gResolution), w), 0);
	L.rgb += AP.rgb;
	float Opacity = AP.a;

	output.Luminance = float4(L, Opacity);
	//output.Luminance *= frac(clamp(w*AP_SLICE_COUNT, 0, AP_SLICE_COUNT));
	*/
}
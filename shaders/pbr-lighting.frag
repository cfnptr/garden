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

// TODO: maybe we can utilize filament micro/macro AO?

#define USE_SPECULAR_FACTOR
#define USE_AMBIENT_OCCLUSION
#define USE_SHADOW_ALPHA
#define USE_CLEAR_COAT
#define USE_LIGHT_EMISSION
#define USE_REFLECTIONS

spec const bool USE_SHADOW_BUFFER = false;
spec const bool USE_AO_BUFFER = false;
spec const bool USE_REFLECTION_BUFFER = false;
spec const bool USE_REFLECTION_BLUR = false;
spec const bool USE_GI_BUFFER = false;

#include "common/pbr.gsl"
#include "common/depth.gsl"

pipelineState
{
	stencilTesting = on;
	stencilCompare = equal;
	stencilWriteMask = 0x00;
	stencilCompareMask = 0x01;
	stencilReference = 0x01;
}

in noperspective float2 fs.texCoords;
out float4 fb.hdr;

uniform sampler2D g0;
uniform sampler2D g1;
uniform sampler2D g2;
uniform sampler2D g3;

uniform sampler2D depthBuffer;
uniform sampler2D shadBuffer;
uniform sampler2D aoBuffer;
uniform sampler2D giBuffer;
uniform sampler2D
{
	filterMipmap = linear;
} reflBuffer;

uniform sampler2D
{
	filter = linear;
} dfgLUT;

uniform set1 samplerCube
{
	filter = linear;
} specular;
uniform set1 ShData
{
	half4x4 diffuse[3];
} sh;

uniform pushConstants
{
	float4x4 uvToWorld;
	float4 shadowColor;
	float emissiveCoeff;
	float reflectanceCoeff;
	float ggxLodOffset;
} pc;

//**********************************************************************************************************************
void main()
{
	GBufferValues gBuffer = decodeGBufferValues(g0, g1, g2, g3, fs.texCoords);
	gBuffer.emissiveColor.a *= pc.emissiveCoeff; gBuffer.reflectance *= pc.reflectanceCoeff;

	if (USE_GI_BUFFER)
	{
		gBuffer.irradiance = textureLod(giBuffer, fs.texCoords, 0.0f).rgb;
	}
	else
	{
		float3 shadowColor = pc.shadowColor.rgb;
		if (USE_SHADOW_BUFFER)
		{
			float4 accumShadow = textureLod(shadBuffer, fs.texCoords, 0.0f);
			shadowColor *= accumShadow.rgb; gBuffer.shadowAlpha = min(gBuffer.shadowAlpha, accumShadow.a);
		}
		shadowColor *= lerp(pc.shadowColor.a, 1.0f, gBuffer.shadowAlpha);

		gBuffer.irradiance = diffuseIrradiance(gBuffer.normal, sh.diffuse) * shadowColor;
	}

	float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).x;
	float4 worldPosition = pc.uvToWorld * float4(fs.texCoords, depth, 1.0f);
	worldPosition.xyz /= worldPosition.w;

	if (USE_AO_BUFFER)
	{
		float ao = textureLod(aoBuffer, fs.texCoords, 0.0f).r;
		gBuffer.ambientOcclusion = min(gBuffer.ambientOcclusion, ao);
	}
	if (USE_REFLECTION_BUFFER)
	{
		float lod = USE_REFLECTION_BLUR ? calcGgxLod(gBuffer.roughness, 
			length(worldPosition.xyz), pc.ggxLodOffset) : 0.0f;
		gBuffer.reflectionColor = textureLod(reflBuffer, fs.texCoords, lod);
	}

	gBuffer.viewDirection = calcViewDirection(worldPosition.xyz);
	BrdfTerms terms = evaluateBRDF(gBuffer, dfgLUT, specular);
	fb.hdr = float4(terms.fd + terms.fr + terms.fe, 1.0f);
}
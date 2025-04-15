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

spec const bool USE_EMISSIVE_BUFFER = false;
spec const bool USE_SUB_SURFACE_SCATTERING = false;

#include "common/gbuffer.gsl"

pipelineState
{
	faceCulling = off;
	colorMask2 = r;
	colorMask3 = r;
}

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;
out float4 fb.g4;
out float4 fb.g5;

uniform pushConstants
{
	float4 colorSpec;
	float4 mraor;
	float4 emissive;
	float4 subsurface;
	float shadow;
	float ccRoughness;
} pc;

void main()
{
	GBufferValues values = gBufferValuesDefault();
	values.baseColor = pc.colorSpec.rgb;
	values.specularFactor = pc.colorSpec.a;
	values.metallic = pc.mraor.r;
	values.roughness = pc.mraor.g;
	values.ambientOcclusion = pc.mraor.b;
	values.reflectance = pc.mraor.a;
	values.clearCoatRoughness = pc.ccRoughness;
	values.shadow = pc.shadow;

	if (USE_EMISSIVE_BUFFER)
	{
		values.emissiveColor = pc.emissive.rgb;
		values.emissiveFactor = pc.emissive.a;
	}
	if (USE_SUB_SURFACE_SCATTERING)
	{
		values.subsurfaceColor = pc.subsurface.rgb;
		values.thickness = pc.subsurface.a;
	}

	ENCODE_G_BUFFER_VALUES(values);
}
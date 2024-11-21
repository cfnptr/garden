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

#include "common/gbuffer.gsl"

pipelineState
{
	faceCulling = off;
	colorMask2 = r;
}

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;
out float4 fb.g4;

uniform pushConstants
{
	float4 color;
	float4 mraor;
	float4 emissive;
	float4 subsurface;
	float clearCoat;
} pc;

void main()
{
	GBufferValues values;
	values.baseColor = pc.color;
	values.metallic = pc.mraor.r;
	values.roughness = pc.mraor.g;
	values.ambientOcclusion = pc.mraor.b;
	values.reflectance = pc.mraor.a;
	values.normal = float3(0.0f); // Using color mask here
	values.clearCoat = pc.clearCoat;
	values.emissiveColor = pc.emissive.rgb;
	values.exposureWeight = pc.emissive.a;
	values.subsurfaceColor = pc.subsurface.rgb;
	values.thickness = pc.subsurface.a;
	encodeGBufferValues(values, fb.g0, fb.g1, fb.g2, fb.g3, fb.g4);
}
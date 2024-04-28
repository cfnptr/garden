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
	colorMask1 = b;
}

out float4 fb.gBuffer0;
out float4 fb.gBuffer1;
out float4 fb.gBuffer2;

uniform pushConstants
{
	float4 baseColor;
	float4 emissive;
	float metallic;
	float roughness;
	float reflectance;
} pc;

void main()
{
	fb.gBuffer0 = encodeGBuffer0(pc.baseColor.rgb, pc.metallic); 
	fb.gBuffer1 = float4(0.0f, 0.0f, pc.reflectance, 0.0f);
	fb.gBuffer2 = encodeGBuffer2(pc.emissive.rgb, pc.roughness);
}
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

#include "common/tone-mapping.gsl"

in float2 vs.position : f32;
in float2 vs.texCoords : f32;
in float4 vs.color : f8;

out float4 fs.color;
out float2 fs.texCoords;

uniform pushConstants
{
	float2 scale;
	float2 translate;
} pc;

void main()
{
	float3 color = gammaCorrection(vs.color.rgb, DEFAULT_GAMMA);
	fs.color = float4(color, vs.color.a);
	fs.texCoords = vs.texCoords;
	gl.position = float4(vs.position * pc.scale + pc.translate, 0.0f, 1.0f);
}
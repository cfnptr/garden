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

#include "common/tone-mapping.gsl"

vertexBuffer
{
	float2 position : f32;
	float2 texCoords : unorm16;
	float4 color : unorm8;
}

uniform pushConstants
{
	float2 scale;
	float2 translate;
} pc;

out float4 fs.color;
out float2 fs.texCoords;

void main()
{
	gl.position = float4(vs.position * pc.scale + pc.translate, 0.0f, 1.0f);
	fs.color = float4(gammaCorrection(vs.color.rgb, DEFAULT_GAMMA), vs.color.a);
	fs.texCoords = vs.texCoords;
}
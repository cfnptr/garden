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

#include "text/common.gsl"
#include "common/color-space.gsl"

out float2 fs.texCoords;
out flat float4 fs.color;
out flat uint32 fs.atlasIndex;

uniform pushConstants
{
	float4x4 mvp;
	float4 color;
} pc;

buffer readonly Instance
{
	TextInstanceData data[];
} instance;

void main()
{
	TextInstanceData instance = instance.data[gl.instanceIndex];
	float4 position = instance.position * textVertPosMuls[gl.vertexIndex];
	gl.position = pc.mvp * float4(position.xy + position.zw, 0.0f, 1.0f);
	float4 texCoords = instance.texCoords * textTexCoordMuls[gl.vertexIndex];
	fs.texCoords = texCoords.xy + texCoords.zw;
	fs.color = srgbToRgb(instance.srgbColor);
	fs.atlasIndex = instance.atlasIndex;
}
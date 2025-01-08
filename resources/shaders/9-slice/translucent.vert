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

#include "common/primitives.gsl"

out float2 fs.texCoords;

uniform pushConstants
{
	uint32 instanceIndex;
	float colorMapLayer;
} pc;

struct InstanceData
{
	float4x4 mvp;
	float4 colorFactor;
	float4 sizeOffset;
	float4 texWinBorder;
};
buffer readonly Instance
{
	InstanceData data[];
} instance;

void main()
{
	float4 position = float4(quadVertices[gl.vertexIndex], 0.0f, 1.0f);
	gl.position = instance.data[pc.instanceIndex].mvp * position;
	fs.texCoords = quadTexCoords[gl.vertexIndex];
}
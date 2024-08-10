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

#include "9-slice/common.gsl"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

in float2 fs.texCoords;
out float4 fb.color;

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

uniform set1 sampler2DArray
{
	filter = linear;
	wrap = repeat;
} colorMap;

void main()
{
	float4 colorFactor = instance.data[pc.instanceIndex].colorFactor;
	float2 uvSize = instance.data[pc.instanceIndex].sizeOffset.xy;
	float2 uvOffset = instance.data[pc.instanceIndex].sizeOffset.zw;
	float2 textureBorder = instance.data[pc.instanceIndex].texWinBorder.xy;
	float2 windowBorder = instance.data[pc.instanceIndex].texWinBorder.zw;
	float3 texCoords = float3(fs.texCoords * uvSize + uvOffset, pc.colorMapLayer);
	texCoords.x = mapAxis(texCoords.x, textureBorder.x, windowBorder.x);
	texCoords.y = mapAxis(texCoords.y, textureBorder.y, windowBorder.y);
	fb.color = texture(colorMap, texCoords) * colorFactor;
}
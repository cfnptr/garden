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

#include "9-slice/common.gsl"

in float2 fs.texCoords;
out float4 fb.color;

uniform pushConstants
{
	uint32 instanceIndex;
	float colorMapLayer;
} pc;

buffer readonly Instance
{
	InstanceData data[];
} instance;

uniform set1 sampler2DArray
{
	addressMode = repeat;
	filter = linear;
} colorMap;

void main()
{
	InstanceData instance = instance.data[pc.instanceIndex];
	float3 texCoords = float3(fs.texCoords * instance.uvSize + instance.uvOffset, pc.colorMapLayer);
	texCoords.x = mapAxis(texCoords.x, instance.textureBorder.x, instance.windowBorder.x);
	texCoords.y = mapAxis(texCoords.y, instance.textureBorder.y, instance.windowBorder.y);
	fb.color = texture(colorMap, texCoords) * instance.color;
}
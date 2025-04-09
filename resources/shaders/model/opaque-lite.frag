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

#include "model/common.gsl"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

in float2 fs.texCoords;
in float3 fs.normal;

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;
out float4 fb.g4;
out float4 fb.g5;

buffer readonly Instance
{
	InstanceData data[];
} instance;

uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} colorMap;
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} mraorMap;
uniform set1 sampler2D
{
	filter = linear;
	addressMode = repeat;
} emissiveMap;

void main()
{
	float4 color = texture(colorMap, fs.texCoords);
	const GBufferValues values = fillModelGBufferLite(
		mraorMap, emissiveMap, fs.texCoords, color, fs.normal);
	ENCODE_G_BUFFER_VALUES(values);
}
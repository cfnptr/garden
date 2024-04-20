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

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform pushConstants
{
	float4 colorFactor;
	uint32 instanceIndex;
	float alphaCutoff;
} pc;

uniform set1 sampler2D
{
	filter = linear;
	wrap = repeat;
} colorMap;

void main()
{
	float4 color = texture(colorMap, fs.texCoords) * pc.colorFactor;
	if (color.a < pc.alphaCutoff)
		discard;
	fb.color = color; 
}
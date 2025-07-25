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

pipelineState
{
	depthTesting = on;
	depthCompare = greaterOrEqual;
	frontFace = clockwise;
}

in float3 fs.texCoords;
out float4 fb.hdr;

uniform samplerCube
{
	filter = linear;
} cubemap;

void main()
{
	float3 color = textureLod(cubemap, fs.texCoords, 0.0f).rgb;
	fb.hdr = float4(color, 1.0f);
}
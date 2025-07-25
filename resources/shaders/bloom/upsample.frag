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
	faceCulling = off;
	blending0 = on;
	srcColorFactor0 = one;
	dstColorFactor0 = one;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D
{
	filter = linear;
} srcBuffer;

void main()
{
	float3 c0, c1; // tent filter
	c0  = textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2(-1, -1)).rgb;
	c0 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2( 1, -1)).rgb;
	c0 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2( 1,  1)).rgb;
	c0 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2(-1,  1)).rgb;
	c0 += textureLod(srcBuffer, fs.texCoords, 0.0f).rgb * 4.0f;
	c1  = textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2(-1,  0)).rgb;
	c1 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2( 0, -1)).rgb;
	c1 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2( 1,  0)).rgb;
	c1 += textureLodOffset(srcBuffer, fs.texCoords, 0.0f, int2( 0,  1)).rgb;
	fb.color = float4((c0 + c1 * 2.0f) * (1.0f / 16.0f), 0.0f);
}
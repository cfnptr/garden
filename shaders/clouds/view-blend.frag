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

#include "common/depth.gsl"

pipelineState
{
	blending0 = on;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D depthBuffer;

uniform sampler2D 
{
	filter = linear;
} cloudsBuffer;
uniform sampler2D 
{
	filter = linear;
} cloudsDepth;

void main()
{
	float depth = textureLod(depthBuffer, fs.texCoords, 0.0f).x;
	float cloudsDepth = textureLod(cloudsDepth, fs.texCoords, 0.0f).x;
	if (IS_DEPTH_GREATER(depth, cloudsDepth))
		discard;

	float4 color = textureLod(cloudsBuffer, fs.texCoords, 0.0f);
	if (IS_DEPTH_GREATER(depth, FAR_PLANE_DEPTH)) // Smoothing sharp transition.
		color.a *= saturate((cloudsDepth - depth) * 1000000.0f);
	fb.color = color;
}
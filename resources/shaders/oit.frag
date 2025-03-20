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

// Weighted Blended Order Independent Transparency
// Based on this: https://learnopengl.com/Guest-Articles/2020/OIT/Weighted-Blended

#include "common/math.gsl"

#define EPSILON 0.00001f

pipelineState
{
	faceCulling = off;
	blending0 = on;
}

out float4 fb.hdr;

uniform sampler2D accumBuffer;
uniform sampler2D revealBuffer;

bool isApproximatelyEqual(float a, float b)
{
    return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * EPSILON;
}

//**********************************************************************************************************************
void main()
{
	float revealage = texelFetch(revealBuffer, int2(gl.fragCoord.xy), 0).r;
	if (isApproximatelyEqual(revealage, 1.0f))
		discard;

	float4 accumulation = texelFetch(accumBuffer, int2(gl.fragCoord.xy), 0);
	if (isinf(max(abs(accumulation.rgb))))
		accumulation.rgb = float3(accumulation.a);

	float3 averageColor = accumulation.rgb / max(accumulation.a, EPSILON);
	fb.hdr = float4(averageColor, 1.0f - revealage);
}
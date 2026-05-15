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

// Based on this: https://github.com/iryoku/smaa/blob/master/SMAA.hlsl

#include "common/tone-mapping.gsl"

spec const float THRESHOLD = 0.05f;
#define LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0f

in noperspective float2 fs.texCoords;
in noperspective float4 fs.offset0;
in noperspective float4 fs.offset1;
in noperspective float4 fs.offset2;

out float4 fb.edges;

uniform sampler2D ldrBuffer;

//**********************************************************************************************************************
void main()
{
	float l     = calcLum(textureLod(ldrBuffer, fs.texCoords,  0.0f).rgb);
	float leftL = calcLum(textureLod(ldrBuffer, fs.offset0.xy, 0.0f).rgb);
	float topL  = calcLum(textureLod(ldrBuffer, fs.offset0.zw, 0.0f).rgb);

	// We do the usual threshold:
	float4 delta;
	delta.xy = abs(l - float2(leftL, topL));
	float2 edges = step(float2(THRESHOLD), delta.xy);

	// Then discard if there is no edge:
	if (dot(edges, float2(1.0f)) == 0.0f)
		discard;

	// Calculate right and bottom deltas:
	float rightL  = calcLum(textureLod(ldrBuffer, fs.offset1.xy, 0.0f).rgb);
	float bottomL = calcLum(textureLod(ldrBuffer, fs.offset1.zw, 0.0f).rgb);
	delta.zw = abs(l - float2(rightL, bottomL));

	// Calculate the maximum delta in the direct neighborhood:
	float2 maxDelta = max(delta.xy, delta.zw);

	// Calculate left-left and top-top deltas:
	float leftLeftL = calcLum(textureLod(ldrBuffer, fs.offset2.xy, 0.0f).rgb);
	float topTopL   = calcLum(textureLod(ldrBuffer, fs.offset2.zw, 0.0f).rgb);
	delta.zw = abs(float2(leftL, topL) - float2(leftLeftL, topTopL));

	// Calculate the final maximum delta:
	maxDelta = max(maxDelta.xy, delta.zw);
	float finalDelta = max(maxDelta.x, maxDelta.y);

	// Local contrast adaptation:
	edges.xy *= step(finalDelta, delta.xy * LOCAL_CONTRAST_ADAPTATION_FACTOR);
	fb.edges = float4(edges, float2(0.0f));
}
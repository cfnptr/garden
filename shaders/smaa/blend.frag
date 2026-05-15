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

in noperspective float2 fs.texCoords;
in noperspective float4 fs.offset;

out float4 fb.ldr;

uniform pushConstants
{
	float2 invFrameSize;
} pc;

uniform sampler2D
{
	filter = linear;
} weightsBuffer;
uniform sampler2D
{
	filter = linear;
} ldrBuffer;

//**********************************************************************************************************************
void main()
{
	// Fetch the blending weights for current pixel:
	float4 a;
	a.x =  textureLod(weightsBuffer, fs.offset.xy, 0.0f).a;  // Right
	a.y =  textureLod(weightsBuffer, fs.offset.zw, 0.0f).g;  // Top
	a.wz = textureLod(weightsBuffer, fs.texCoords, 0.0f).xz; // Bottom / Left

	// Is there any blending weight with a value greater than 0.0?
	if (dot(a, float4(1.0f)) < 1e-5f)
		discard;

	bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

	// Calculate the blending offsets:
	float4 blendingOffset = lerp(float4(0.0f, a.y, 0.0f, a.w), float4(a.x, 0.0f, a.z, 0.0f), bool4(h));
	float2 blendingWeight = lerp(a.yw, a.xz, bool2(h));
	blendingWeight /= dot(blendingWeight, float2(1.0f));

	// Calculate the texture coordinates:
	float4 blendingCoord = fma(blendingOffset, float4(pc.invFrameSize, -pc.invFrameSize), fs.texCoords.xyxy);

	// We exploit bilinear filtering to mix current pixel with the chosen neighbor:
	float4 color = textureLod(ldrBuffer, blendingCoord.xy, 0.0f) * blendingWeight.x;
	fb.ldr = fma(textureLod(ldrBuffer, blendingCoord.zw, 0.0f), blendingWeight.yyyy, color);
}
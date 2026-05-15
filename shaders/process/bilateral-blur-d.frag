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

spec const uint32 KERNEL_RADIUS = 3;
#define BLUR_SIGMA (KERNEL_RADIUS / 2.0f)
#define BLUR_FALLOFF ((1.0f / (-2.0f * BLUR_SIGMA * BLUR_SIGMA)))

in noperspective float2 fs.texCoords;
out float4 fb.data;

uniform sampler2D srcBuffer;
uniform sampler2D depthBuffer;

uniform pushConstants
{
	float2 texelSize;
	float nearPlane;
	float sharpness;
} pc;

float4 depthBilateralBlur(float2 texCoords, float r2, float depth, inout float totalWeight)
{
	float d = calcLinearDepthIRZ(textureLod(depthBuffer, texCoords, 0.0f).x, pc.nearPlane);
	float4 c = textureLod(srcBuffer, texCoords, 0.0f); float diff = (d - depth);
	float w = exp2(fma(r2, BLUR_FALLOFF, diff * diff * pc.sharpness)); totalWeight += w;
	return c * w;
}

void main()
{
	float depth = calcLinearDepthIRZ(textureLod(depthBuffer, fs.texCoords, 0.0f).x, pc.nearPlane);
	float4 totalColor = textureLod(srcBuffer, fs.texCoords, 0.0f);
	float totalWeight = 1.0f;

	for (int32 r = 1; r <= KERNEL_RADIUS; r++)
	{
		float2 offset = pc.texelSize * r; float r2 = float(r * r);
		totalColor += depthBilateralBlur(fs.texCoords + offset, r2, depth, totalWeight);
		totalColor += depthBilateralBlur(fs.texCoords - offset, r2, depth, totalWeight);
	}

	fb.data = totalColor / totalWeight;
}
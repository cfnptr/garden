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

#include "common/depth.gsl"

// TODO: spec const
const uint32 KERNEL_RADIUS = 5;
const float BLUR_SIGMA = KERNEL_RADIUS * 0.5f;
const float BLUR_FALLOFF = 1.0f / (-2.0f * BLUR_SIGMA * BLUR_SIGMA);

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.data;

uniform sampler2D
{
	filter = linear;
} srcBuffer;

uniform sampler2D hizBuffer;

uniform pushConstants
{
	float2 texelSize;
	float nearPlane;
	float sharpness;
} pc;

float4 depthBilateralBlur(float2 texCoords, uint32 r, float depth, inout float weight)
{
	float d = textureLod(hizBuffer, texCoords, 0.0f).r;
	d = calcLinearDepthIRZ(d, pc.nearPlane);
	float4 c = textureLod(srcBuffer, texCoords, 0.0f);
	float diff = (d - depth) * pc.sharpness;
	float w = exp2((r * r * BLUR_FALLOFF) - diff * diff);
	weight += w;
	return c * w;
}

void main()
{
	float depth = textureLod(hizBuffer, fs.texCoords, 0.0f).r;
	depth = calcLinearDepthIRZ(depth, pc.nearPlane);
	float4 sum = textureLod(srcBuffer, fs.texCoords, 0.0f);
	float weight = 1.0f;

	for (int32 r = 1; r <= KERNEL_RADIUS; r++)
	{
		float2 offset = pc.texelSize * r;
		sum += depthBilateralBlur(fs.texCoords + offset, r, depth, weight);
		sum += depthBilateralBlur(fs.texCoords - offset, r, depth, weight);
	}

	fb.data = sum / weight;
}
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

#include "common/math.gsl"
#include "process/gaussian-blur.h"

#variantCount 2

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
buffer restrict readonly KernelBuffer
{
	float2 coeffs[];
} kernel;

uniform pushConstants
{
	float2 texelSize;
	uint32 count;
} pc;

void sampleReinhard(inout float4 sum, inout float weight, float k, float2 texCoords)
{
	float4 s = textureLod(srcBuffer, texCoords, 0.0f);
	float w = k / (1.0f + max(s));
	weight += w; sum += s * w;
}

void main()
{
	const float2 texelSize2 = pc.texelSize * 2.0f;
	float2 offset = pc.texelSize;
	
	float4 sum;
	if (gsl.variant == GAUSSIAN_BLUR_REINHARD)
	{
		sum = float4(0.0f); float weight = 0.0;
		sampleReinhard(sum, weight, kernel.coeffs[0].x, fs.texCoords);

		for (int32 i = 1; i < pc.count; i++, offset += texelSize2)
		{
			float k = kernel.coeffs[i].x;
			float2 o = fma(pc.texelSize, float2(kernel.coeffs[i].y), offset);
			sampleReinhard(sum, weight, k, fs.texCoords + o);
			sampleReinhard(sum, weight, k, fs.texCoords - o);
		}
		sum /= weight;
	}
	else // gsl.variant == GAUSSIAN_BLUR_BASE
	{
		sum = textureLod(srcBuffer, fs.texCoords, 0.0f) * kernel.coeffs[0].x;

		for (int32 i = 1; i < pc.count; i++, offset += texelSize2)
		{
			float k = kernel.coeffs[i].x;
			float2 o = fma(pc.texelSize, float2(kernel.coeffs[i].y), offset);
			sum += textureLod(srcBuffer, fs.texCoords + o, 0.0f) * k;
			sum += textureLod(srcBuffer, fs.texCoords - o, 0.0f) * k;
		}
	}
	fb.data = sum;
}
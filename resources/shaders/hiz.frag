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

// TODO: support different depth buffer data format.

#include "hiz/variants.h"
#include "common/depth.gsl"

#variantCount 2

pipelineState
{
	faceCulling = off;
}

uniform pushConstants
{
	float nearPlane;
} pc;

uniform sampler2D srcBuffer;
out float4 fb.depth;

void main()
{
	float depth;

	if (gsl.variant == HIZ_VARIANT_BASE)
	{
		int2 fragCoords = int2(gl.fragCoord.xy) * 2;
		const float2 srcSize = 1.0f / float2(textureSize(srcBuffer, 0));
		float2 texCoords = (float2(fragCoords) + 0.5f) * srcSize;
		float4 d = textureGather(srcBuffer, texCoords, 0);
		depth = max(max(d.x, d.y), max(d.z, d.w));

		bool2 isPrevLevelOdd = equal(textureSize(srcBuffer, 0) & 1, int2(1));
		if (isPrevLevelOdd.x)
		{
			float4 c = textureGatherOffset(srcBuffer, texCoords, int2(1, 0), 0);
			if (isPrevLevelOdd.x)
			{
				int2 coords = max(fragCoords + int2(2, 2), textureSize(srcBuffer, 0) - 1);
				depth = max(depth, texelFetch(srcBuffer, coords, 0).r);
			}
			depth = max(depth, max(c.y, c.z));
		}
		if (isPrevLevelOdd.y)
		{
			float4 c = textureGatherOffset(srcBuffer, texCoords, int2(0, 1), 0);
			depth = max(depth, max(c.x, c.y));
		}
	}
	else // gsl.variant == HIZ_VARIANT_FIRST
	{
		
		float d = texelFetch(srcBuffer, int2(gl.fragCoord.xy), 0).r;
		depth = calcLinearDepthIRZ(d, pc.nearPlane);
	}

	fb.depth = float4(depth);
}
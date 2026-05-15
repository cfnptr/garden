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

#include "hiz/variants.h"
#include "common/depth.gsl"

#variantCount 2

uniform sampler2D srcBuffer;
out float4 fb.minMax;

void main()
{
	float2 minMax;

	if (gsl.variant == HIZ_VARIANT_BASE)
	{
		const float2 srcSize = 1.0f / float2(textureSize(srcBuffer, 0));
		int2 fragCoords = int2(gl.fragCoord.xy) * 2;
		float2 texCoords = (float2(fragCoords) + 0.5f) * srcSize;
		minMax.x = MIN_DEPTH_X(textureGather(srcBuffer, texCoords, 0));
		minMax.y = MAX_DEPTH_X(textureGather(srcBuffer, texCoords, 1));

		bool2 isPrevLevelOdd = equal(textureSize(srcBuffer, 0) & 1, int2(1));
		if (isPrevLevelOdd.x)
		{
			float4 minD = textureGatherOffset(srcBuffer, texCoords, int2(1, 0), 0);
			float4 maxD = textureGatherOffset(srcBuffer, texCoords, int2(1, 0), 1);
			minMax.x = MIN_DEPTH(minMax.x, MIN_DEPTH(minD.y, minD.z));
			minMax.y = MAX_DEPTH(minMax.y, MAX_DEPTH(maxD.y, maxD.z));

			if (isPrevLevelOdd.y)
			{
				float2 d = textureLodOffset(srcBuffer, texCoords, 0.0f, int2(2, 2)).xy;
				minMax = float2(MIN_DEPTH(minMax.x, d.x), MAX_DEPTH(minMax.y, d.y));
			}
		}
		if (isPrevLevelOdd.y)
		{
			float4 minD = textureGatherOffset(srcBuffer, texCoords, int2(0, 1), 0);
			float4 maxD = textureGatherOffset(srcBuffer, texCoords, int2(0, 1), 1);
			minMax.x = MIN_DEPTH(minMax.x, MIN_DEPTH(minD.y, minD.z));
			minMax.y = MAX_DEPTH(minMax.y, MAX_DEPTH(maxD.y, maxD.z));
		}
	}
	else // gsl.variant == HIZ_VARIANT_FIRST
	{
		minMax = float2(texelFetch(srcBuffer, int2(gl.fragCoord.xy), 0).x);
	}

	fb.minMax = float4(minMax, float2(0.0f));
}
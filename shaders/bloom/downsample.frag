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

#variantCount 2

spec const bool USE_THRESHOLD = false;

#include "bloom/common.gsl"
#include "bloom/variants.h"

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D
{
	filter = linear;
} srcBuffer;

uniform pushConstants
{
	float threshold;
} pc;

void main()
{
	float3 color;
	if (gsl.variant == BLOOM_DOWNSAMPLE_BASE)
		color = downsample(srcBuffer, fs.texCoords);
	else if (gsl.variant == BLOOM_DOWNSAMPLE_6X6)
		color = downsample6x6(srcBuffer, fs.texCoords);

	if (USE_THRESHOLD)
		color = any(lessThan(color, float3(pc.threshold))) ? float3(0.0f) : color;
	fb.color = float4(color, 0.0f);
}
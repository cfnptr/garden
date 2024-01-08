//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "bloom/common.gsl"
#include "common/depth.gsl"
#include "common/gbuffer.gsl"

spec const bool USE_THRESHOLD = false;
spec const bool USE_ANTI_FLICKERING = true;

pipelineState
{
	faceCulling = off;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D
{
	filter = linear;
	wrap = clampToEdge;
} srcTexture;

uniform pushConstants
{
	float threshold;
} pc;

//--------------------------------------------------------------------------------------------------
void main()
{
	float3 color = downsample(srcTexture, fs.texCoords,
		pc.threshold, USE_THRESHOLD, USE_ANTI_FLICKERING);
	fb.color = float4(max(color, 0.0001f), 0.0f);
}
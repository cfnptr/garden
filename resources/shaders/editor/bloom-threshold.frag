//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

pipelineState
{
	faceCulling = off;
	blending0 = on;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D hdrBuffer;

uniform pushConstants
{
	float threshold;
} pc;

//--------------------------------------------------------------------------------------------------
void main()
{
	float3 hdrColor = texture(hdrBuffer, fs.texCoords).rgb;
	float3 color = downsample(hdrBuffer, fs.texCoords, 0.0f, true);
	if (any(lessThan(color, float3(pc.threshold))))
		fb.color = float4(0.8f, 0.0f, 0.0f, 0.5f);
	else discard;
}
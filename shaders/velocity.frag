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

#include "common/gbuffer.gsl"
#include "common/velocity.gsl"
#include "common/constants.gsl"

pipelineState
{
	depthTesting = on;
	depthCompare = greaterOrEqual;

	// Note: should match G-Buffer!
	colorMask0 = none;
	colorMask1 = none;
	colorMask2 = none;
}

in noperspective float2 fs.currNdcPos;

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;

void main()
{
	float3 currWorldPos = (cc.invViewProj * float4(fs.currNdcPos, 0.0f, 1.0f)).xyz;
	float4 prevNdcPos = cc.prevViewProj * float4(normalize(currWorldPos), 0.0f);
	float2 velocity = calcVelocity(fs.currNdcPos, prevNdcPos.xy / 
		prevNdcPos.w, cc.jitterOffset, cc.prevJitterOffset);
	fb.g3 = float4(velocity, float2(0.0f));
}
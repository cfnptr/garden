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

in noperspective float2 fs.texCoords;
out float4 fb.disoccl;

uniform sampler2D prevDepthBuffer;
uniform sampler2D currDepthBuffer;
uniform sampler2D gVelocity;

uniform pushConstants 
{
	float nearPlane;
	float threshold;
	float velFactor;
} pc;

// TODO: take into account TAA jittering.

void main()
{
	float predDepth = calcLinearDepthIRZ(textureLod(prevDepthBuffer, fs.texCoords, 0.0f).x, pc.nearPlane);
	float currDepth = calcLinearDepthIRZ(textureLod(currDepthBuffer, fs.texCoords, 0.0f).x, pc.nearPlane);
	float2 velocity = textureLod(gVelocity, fs.texCoords, 0.0f).xy;
	float threshold = (pc.threshold * TO_ONE_DEPTH(currDepth)) + (length(velocity) * pc.velFactor);
	fb.disoccl = float4(abs(predDepth - currDepth) > threshold ? 1.0f : 0.0f, float3(0.0f));
}
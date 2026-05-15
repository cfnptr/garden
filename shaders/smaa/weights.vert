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

// Based on this: https://github.com/iryoku/smaa/blob/master/SMAA.hlsl

#include "common/fullscreen.gsl"

spec const float MAX_SEARCH_STEPS = 16.0f;

out noperspective float2 fs.texCoords;
out noperspective float2 fs.pixCoords;
out noperspective float4 fs.offset0;
out noperspective float4 fs.offset1;
out noperspective float4 fs.offset2;

uniform pushConstants
{
	float2 invFrameSize;
	float2 frameSize;
} pc;

void main()
{
	fs.texCoords = toFullscreenTexCoords(gl.vertexIndex);
	gl.position = float4(toFullscreenPosition(fs.texCoords), 1.0f);
	fs.pixCoords = fs.texCoords * pc.frameSize;

	// We will use these offsets for the searches later on:
	fs.offset0 = fma(pc.invFrameSize.xyxy, float4(-0.25f,  -0.125f,  1.25f,  -0.125f), fs.texCoords.xyxy);
	fs.offset1 = fma(pc.invFrameSize.xyxy, float4(-0.125f, -0.25f,  -0.125f,  1.25f),  fs.texCoords.xyxy);

	// And these for the searches, they indicate the ends of the loops:
	fs.offset2 = fma(pc.invFrameSize.xxyy, float4(-2.0f, 2.0f, -2.0f, 2.0f) * 
		MAX_SEARCH_STEPS, float4(fs.offset0.xz, fs.offset1.yw));
}
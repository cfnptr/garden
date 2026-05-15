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

out noperspective float2 fs.texCoords;
out noperspective float4 fs.offset0;
out noperspective float4 fs.offset1;
out noperspective float4 fs.offset2;

uniform pushConstants
{
	float2 invFrameSize;
} pc;

void main()
{
	fs.texCoords = toFullscreenTexCoords(gl.vertexIndex);
	gl.position = float4(toFullscreenPosition(fs.texCoords), 1.0f);

	fs.offset0 = fma(pc.invFrameSize.xyxy, float4(-1.0f, 0.0f, 0.0f, -1.0f), fs.texCoords.xyxy);
	fs.offset1 = fma(pc.invFrameSize.xyxy, float4( 1.0f, 0.0f, 0.0f,  1.0f), fs.texCoords.xyxy);
	fs.offset2 = fma(pc.invFrameSize.xyxy, float4(-2.0f, 0.0f, 0.0f, -2.0f), fs.texCoords.xyxy);
}
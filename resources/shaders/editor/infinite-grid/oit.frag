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

#include "editor/infinite-grid/common.gsl"
#include "common/oit.gsl"
#include "common/depth.gsl"
#include "common/constants.gsl"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
	faceCulling = off;
	blending0 = on;
	srcBlendFactor0 = one;
	dstBlendFactor0 = one;
	blending1 = on;
	srcBlendFactor1 = zero;
	dstBlendFactor1 = oneMinusSrcColor;
}

uniform pushConstants
{
	float4 meshColor;
	float4 axisColorX;
	float4 axisColorYZ;
	float meshScale;
	bool isHorizontal;
} pc;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;

in float3 fs.nearPoint;
in float3 fs.farPoint;

out float4 fb.accum;
out float fb.reveal;

earlyFragmentTests in;

void main()
{
	float t = intersectGrid(fs.nearPoint, fs.farPoint, pc.isHorizontal);
	if (t <= 0.0f)
		discard;

	float3 fragPos = calcFragPos(fs.nearPoint, fs.farPoint, t);
	float4 gridColor = calcGrid(fragPos, pc.meshColor.rgb, pc.axisColorX.rgb, 
		pc.axisColorYZ.rgb, pc.meshScale, pc.isHorizontal);
	if (gridColor.a == 0.0f)
		discard;

	gl.fragDepth = calcDepth(fragPos - cc.cameraPos.xyz, cc.viewProj) + FLOAT_EPS6;
	computeOIT(gridColor, gl.fragDepth, fb.accum, fb.reveal);
}
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

#include "common/depth.gsl"
#include "common/constants.gsl"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
	faceCulling = off;
	blending0 = on;
}

uniform pushConstants
{
	float4 meshColor;
	float4 axisColorX;
	float4 axisColorYZ;
	float meshScale;
	bool isHorizontal;
} pc;

uniform CameraConstants
{
	CAMERA_CONSTANTS
} cc;

in float3 fs.nearPoint;
in float3 fs.farPoint;

out float4 fb.color;

//**********************************************************************************************************************
float4 grid(float3 fragPos, float3 meshColor, float3 axisColorX, float3 axisColorYZ, float meshScale, bool isHorizontal)
{
	float2 coords = (isHorizontal ? fragPos.xz : fragPos.xy) * meshScale;
	float2 derivative = fwidth(coords);
	float2 grid = abs(fract(coords - 0.5f) - 0.5f) / derivative;
	float line = min(grid.x, grid.y);
	float2 derMin = min(derivative, float2(1.0f));
	float4 color = float4(meshColor, 1.0f - min(line, 1.0f));
	float scale = 1.0f / meshScale;

	if (fragPos.x > -scale * derMin.x && fragPos.x < scale * derMin.x)
		color.rgb = axisColorX;

	if (isHorizontal)
	{
		if (fragPos.z > -scale * derMin.y && fragPos.z < scale * derMin.y)
			color.rgb = axisColorYZ;
	}
	else
	{
		if (fragPos.y > -scale * derMin.y && fragPos.y < scale * derMin.y)
			color.rgb = axisColorYZ;
	}
	return color;
}

void main()
{
	float t = pc.isHorizontal ? -fs.nearPoint.y / (fs.farPoint.y - fs.nearPoint.y) : 
		-fs.nearPoint.z / (fs.farPoint.z - fs.nearPoint.z);
	if (t <= 0.0f)
		discard;

	float3 fragPos = t * (fs.farPoint - fs.nearPoint) + fs.nearPoint;
	fb.color = grid(fragPos, pc.meshColor.rgb, pc.axisColorX.rgb,
		pc.axisColorYZ.rgb, pc.meshScale, pc.isHorizontal);
	if (fb.color.a == 0.0f)
		discard;

	gl.fragDepth = calcDepth(fragPos - cc.cameraPos.xyz, cc.viewProj);
}
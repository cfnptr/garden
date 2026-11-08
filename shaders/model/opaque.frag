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

#define USE_SPECULAR_FACTOR
#define USE_AMBIENT_OCCLUSION
#define USE_SHADOW_ALPHA
#define USE_LIGHT_EMISSION
#define USE_CLEAR_COAT
#define USE_MOTION_VECTORS
#define USE_VELOCITY_BUFFER true

spec const bool USE_ALPHA_CUTOFF = false;

#include "common/gbuffer.gsl"
#include "common/velocity.gsl"
#include "common/constants.gsl"
#include "model/instance-data.h"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
	stencilTesting = on;
	stencilPass = replace;
	stencilReference = 0x01;
}

in float4 fs.currNdcPos;
in float4 fs.prevNdcPos;
in float2 fs.texCoords;
in float3 fs.normal;
in float3 fs.tangent;
in flat float fs.bitanSign;

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;

uniform pushConstants
{
	uint32 instanceIndex;
} pc;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;
buffer readonly Instance
{
	BaseInstanceData data[];
} instance;

earlyFragmentTests in;

void main()
{
	float4 baseColor = float4(1.0f);

	if (USE_ALPHA_CUTOFF)
	{
		if (baseColor.a < 0.5f)
			discard;
	}

	float3x3 tbn = decodeModelTBN(fs.normal, fs.tangent, fs.bitanSign);

	GBufferValues gBuffer = gBufferValuesDefault();
	gBuffer.velocity = calcVelocity(fs.currNdcPos, fs.prevNdcPos, cc.jitterOffset, cc.prevJitterOffset);
	gBuffer.normal = tbn * float3(0, 0, 1); // TODO: read normal map.
	ENCODE_G_BUFFER_VALUES(gBuffer);
}
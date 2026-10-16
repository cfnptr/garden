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

spec const bool USE_ALPHA_CUTOFF = false;

#include "common/gbuffer.gsl"
#include "model/instance-data.h"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

out float4 fb.g0;
out float4 fb.g1;
out float4 fb.g2;
out float4 fb.g3;

uniform pushConstants
{
	uint32 instanceIndex;
} pc;

buffer readonly Instance
{
	BaseInstanceData data[];
} instance;

void main()
{
	GBufferValues gBuffer = gBufferValuesDefault();
	gBuffer.baseColor = float4(float3(1.0f), 1.0f);
	ENCODE_G_BUFFER_VALUES(gBuffer);
}
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

#include "model/instance-data.h"

pipelineState
{
	depthTesting = on;
	depthWriting = on;
}

out float4 fb.color;

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
	// fb.color = instance.data[pc.instanceIndex].color;
	fb.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
}
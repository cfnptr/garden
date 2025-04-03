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

in float3 vs.position : f32;

uniform pushConstants
{
	uint32 instanceIndex;
} pc;

struct InstanceData
{
	float4x4 mvp;
	float4 color;
};
buffer readonly Instance
{
	InstanceData data[];
} instance;

void main()
{
	gl.position = instance.data[pc.instanceIndex].mvp * float4(vs.position, 1.0f);
}
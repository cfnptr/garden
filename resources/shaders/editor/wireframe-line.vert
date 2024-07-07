// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

uniform pushConstants
{
	float4x4 mvp;
	float4 color;
	float4 startPoint;
	float4 endPoint;
} pc;

void main()
{
	float3 lines[2];
	lines[0] = pc.startPoint.xyz;
	lines[1] = pc.endPoint.xyz;

	float3 vertex = lines[gl.vertexIndex];
	gl.position = pc.mvp * float4(vertex, 1.0f);
}
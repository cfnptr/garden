//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

pipelineState
{
	faceCulling = off;
	blending0 = on;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D depthBuffer;

uniform pushConstants
{
	float4 farPlanes;
} pc;

//--------------------------------------------------------------------------------------------------
void main()
{
	float depth = texture(depthBuffer, fs.texCoords).r;
	if (depth > pc.farPlanes.x) fb.color = float4(0.0f, 0.8f, 0.0f, 0.5f);
	else if (depth > pc.farPlanes.y) fb.color = float4(0.8f, 0.8f, 0.0f, 0.5f);
	else if (depth > pc.farPlanes.z) fb.color = float4(0.8f, 0.0f, 0.0f, 0.5f);
	else fb.color = float4(0.8f, 0.0f, 0.8f, 0.5f);
}
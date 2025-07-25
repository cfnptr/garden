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

out float4 fb.color;

uniform pushConstants
{
	float4x4 mvp;
	float3 color;
	float renderScale;
} pc;

void main()
{
	float2 fragCoord = floor(gl.fragCoord.xy * pc.renderScale);
	bool isEven = mod(fragCoord.x + fragCoord.y, 2.0f) == 0.0f;
	fb.color = float4(isEven ? pc.color * 0.5f : pc.color * 0.25f, 0.0f);
}
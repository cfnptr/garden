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

pipelineState
{
	faceCulling = off;
}

in noperspective float2 fs.texCoords;
out float4 fb.data;

uniform sampler2D srcBuffer;

uniform pushConstants
{
	float2 texelSize;
} pc;

void main()
{
	float4 sum = float4(0.0f);
	for (int32 y = -1; y <= 1; y++)
	{
		for (int32 x = -1; x <= 1; x++)
		{
			float2 offset = float2(x, y) * pc.texelSize;
			sum += textureLod(srcBuffer, fs.texCoords + offset, 0.0f);
		}
	}
	fb.data = sum * (1.0f / 9.0f);
}
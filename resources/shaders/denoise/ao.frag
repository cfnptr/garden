//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#define RADIUS 2

pipelineState
{
	faceCulling = off;
}

in float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D aoBuffer;

//--------------------------------------------------------------------------------------------------
void main()
{
	float2 texelSize = 1.0f / textureSize(aoBuffer, 0);
	float result = 0.0;
	
	for (int32 x = -RADIUS; x < RADIUS; x++) 
	{
		for (int32 y = -RADIUS; y < RADIUS; y++) 
		{
			float2 offset = float2(x, y) * texelSize;
			result += texture(aoBuffer, fs.texCoords + offset).r;
		}
	}
	
	result *= 1.0f / (RADIUS * RADIUS * RADIUS * RADIUS);
	fb.color = float4(result, 0.0f, 0.0f, 0.0f);
}
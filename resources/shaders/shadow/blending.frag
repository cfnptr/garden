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

pipelineState
{
	faceCulling = off;
	blending0 = on;
	srcBlendFactor0 = dstColor;
	dstBlendFactor0 = zero;
}

in float2 fs.texCoords;
out float4 fb.hdr;

uniform sampler2D shadowBuffer;

uniform pushConstants
{
	float4 color;
} pc;

//--------------------------------------------------------------------------------------------------
void main()
{
	float shadow = texture(shadowBuffer, fs.texCoords).r;
	float3 color = mix(pc.color.rgb, float3(1.0f), shadow);
	fb.hdr = float4(color * shadow, 1.0f);
}
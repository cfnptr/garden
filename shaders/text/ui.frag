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

#include "text/common.gsl"

pipelineState
{
	blending0 = on;
}

in float2 fs.texCoords;
in flat float4 fs.color;
in flat uint32 fs.atlasIndex;

out float4 fb.color;

uniform sampler2D
{
	filter = linear;
} fontAtlas;

uniform pushConstants
{
	float4x4 mvp;
	float4 color;
} pc;

void main()
{
	float alpha = texture(fontAtlas, fs.texCoords)[fs.atlasIndex];
	fb.color = float4(fs.color.rgb, fs.color.a * alpha) * pc.color;
}
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

#include "common/tone-mapping.gsl"

pipelineState
{
	faceCulling = off;
	blending0 = on;
}

in noperspective float2 fs.texCoords;
out float4 fb.color;

uniform sampler2D hdrBuffer;

uniform Luminance
{
	float avgLuminance;
	float exposure;
} luminance;
uniform pushConstants
{
	float minLum;
	float maxLum;
} pc;

void main()
{
	float3 hdrColor = textureLod(hdrBuffer, fs.texCoords, 0.0f).rgb;
	float lum = rgbToLum(hdrColor);

	if (lum < pc.minLum)
		fb.color = float4(0.0f, 0.0f, 0.8f, 0.4f);
	else if (lum > pc.maxLum)
		fb.color = float4(0.8f, 0.0f, 0.0f, 0.6f);
	else
		discard;
}
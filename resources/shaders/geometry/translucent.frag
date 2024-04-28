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

#include "common/pbr.gsl"
#include "common/depth.gsl"
#include "common/constants.gsl"

pipelineState
{
	depthTesting = on;
	blending0 = on;
}

in float2 fs.texCoords;
in float3 fs.normal;

out float4 fb.hdr;

uniform pushConstants
{
	float4 baseColor;
	float4 emissive;
	float metallic;
	float roughness;
	float reflectance;
	uint32 instanceIndex;
} pc;

uniform set1 sampler2D
{
	filter = linear;
	wrap = repeat;
} baseColorMap;
uniform set1 sampler2D
{
	filter = linear;
	wrap = repeat;
} ormMap;

uniform sampler2D
{
	filter = linear;
} dfgLUT;
uniform set2 samplerCube
{
	filter = linear;
} specular;

uniform CameraConstants 
{
	CAMERA_CONSTANTS
} cc;
uniform set2 IblData
{
	float4 sh[SH_COEF_COUNT];
} data;
// TODO: support multiple specular/sh buffer count.

//**********************************************************************************************************************
void main()
{
	float4 baseColor = texture(baseColorMap, fs.texCoords) * pc.baseColor;
	float4 orm = texture(ormMap, fs.texCoords);

	PbrMaterial pbrMaterial; // TODO: define emissive strength in candela, lumens or wats.
	pbrMaterial.baseColor = baseColor.rgb * (pc.emissive.rgb + 1.0f);
	pbrMaterial.metallic = orm.b * pc.metallic;
    pbrMaterial.roughness = orm.g * pc.roughness;
	pbrMaterial.reflectance = pc.reflectance;
	pbrMaterial.viewDirection = calcViewDirection(gl.fragCoord.z, fs.texCoords, cc.viewProjInv);
	pbrMaterial.normal = normalize(fs.normal);
	pbrMaterial.shadow = 1.0f;
	// TODO: shadow value and shadow color.

	float3 hdrColor = float3(0.0f);
	hdrColor += evaluateIBL(pbrMaterial, dfgLUT, data.sh, specular);
	//hdrColor += float3(0.001f) * pbrMaterial.baseColor; // ambient

	float obstruction = gl.fragCoord.z < (1.0f - FLOAT_EPS6) ? 1.0f : 0.0f;
	fb.hdr = float4(hdrColor * obstruction, baseColor.a);
}
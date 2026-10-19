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

#include "common/constants.gsl"
#include "model/instance-data.h"
#include "common/normal-mapping.gsl"

vertexBuffer0
{
	float3 position : unorm16;
	uint32 flags : u16;
}
vertexBuffer1
{
	float2 normal : unorm16;
	float2 tangent : unorm16;
	float2 texCoords : unorm16;
	float4 color : unorm8;
}

out flat float3x3 fs.tbn;

uniform pushConstants
{
	uint32 instanceIndex;
} pc;

uniform CommonConstants
{
	COMMON_CONSTANTS
} cc;
buffer readonly Instance
{
	BaseInstanceData data[];
} instance;

float3 decodeModelNormal(uint32 flags, float2 encNormal)
{
	return decodeNormalOct3(float3(encNormal, flags & MODEL_VERT_FLAG_NORM_SIGN));
}
float3 decodeModelTangent(uint32 flags, float2 encTangent)
{
	return decodeNormalOct3(float3(encTangent, flags & MODEL_VERT_FLAG_TAN_SIGN));
}
float3 decodeModelBitangent(uint32 flags, float3 normal, float3 tangent)
{
	return cross(normal, tangent) * ((flags & MODEL_VERT_FLAG_BITAN_SIGN) > 0 ? 1.0f : -1.0f);
}
float3x3 decodeModelTBN(uint32 flags, float2 encNormal, float2 encTangent)
{
	float3 normal = decodeModelNormal(flags, encNormal);
	float3 tangent = decodeModelTangent(flags, encTangent);
	float3 bitangent = decodeModelBitangent(flags, normal, tangent);
	return createTBN(normal, tangent, bitangent);
}

void main()
{
	float4x3 model = transpose(instance.data[pc.instanceIndex].currModel);
	fs.tbn = decodeModelTBN(vs.flags, vs.normal, vs.tangent);
	gl.position = cc.viewProj * float4x4(model) * float4(vs.position, 1.0f);
}
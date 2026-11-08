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

#ifndef MODEL_INSTANCE_DATA_H
#define MODEL_INSTANCE_DATA_H

#include "garden/math.h"

#define MODEL_VERT_FLAG_NONE 0x0000
#define MODEL_VERT_FLAG_NORM_SIGN 0x0001
#define MODEL_VERT_FLAG_TAN_SIGN 0x0002
#define MODEL_VERT_FLAG_BITAN_SIGN 0x0004
#define MODEL_VERT_FLAG_COUNT 3

struct BaseInstanceData
{
	float3x4 currModel;
	half2 uvSize;
	half2 uvOffset;
	Color colorAdd;
	Color colorMul;
	float3x4 prevModel;
	Color ormAdd;
	Color ormMul;
	uint32 _alignment0;
	uint32 _alignment1;
};
struct ShadowInstanceData
{
	float3x4 model;
	half2 uvSize;
	half2 uvOffset;
	uint32 _alignment0;
	uint32 _alignment1;
};

#if defined(GRAPHICS_GSL)
#include "common/normal-mapping.gsl"

float4x4 decodeModelMatrix(float3x4 model) { return float4x4(transpose(model)); }
float3 decodeModelNormal(uint32 flags, float2 encNormal)
{
	return decodeNormalOct3(float3(encNormal, flags & MODEL_VERT_FLAG_NORM_SIGN));
}
float3 decodeModelTangent(uint32 flags, float2 encTangent)
{
	return decodeNormalOct3(float3(encTangent, flags & MODEL_VERT_FLAG_TAN_SIGN));
}
float decodeModelBitanSign(uint32 flags)
{
	return (flags & MODEL_VERT_FLAG_BITAN_SIGN) > 0 ? 1.0f : -1.0f;
}
float3 decodeModelBitangent(float3 normal, float3 tangent, float bitanSign)
{
	return cross(normal, tangent) * bitanSign;
}
float3x3 decodeModelTBN(float3 normal, float3 tangent, float bitanSign)
{
	float3 bitangent = decodeModelBitangent(normal, tangent, bitanSign);
	return createTBN(normal, tangent, bitangent);
}
#endif

#endif // MODEL_INSTANCE_DATA_H
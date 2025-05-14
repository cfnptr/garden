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

/***********************************************************************************************************************
 * @file
 * @brief Garden Shading Language - Custom GLSL dialect.
 */

#pragma once
#include "garden/defines.hpp"

namespace garden::graphics
{

/**
 * @brief GSL data types. (Garden Shading Language)
 * @details Data types that can be used to define variables and manipulate data within shaders.
 * 
 * Basic types: bool, int32, uint32, float.
 * Vector types: boolX, intX, uintX, floatX.
 * Matrix types: floatXxX.
 */
enum class GslDataType : uint8
{
	Bool,     /**< Boolean type, true / false. (32-bit integer internally) */
	Int32,    /**< 32-bit signed integer type. (4 bytes) */
	Uint32,   /**< 32-bit unsigned integer type. (4 bytes) */
	Float,    /**< 32-bit floating point type. (IEEE-754) */
	Bool2,    /**< 2D boolean type. (2D 32-bit integer internally) */
	Bool3,    /**< 3D boolean type. (3D 32-bit integer internally) */
	Bool4,    /**< 4D boolean type. (4D 32-bit integer internally) */
	Int2,     /**< 2D 32-bit signed integer type. */
	Int3,     /**< 3D 32-bit signed integer type. */
	Int4,     /**< 4D 32-bit signed integer type. */
	Uint2,    /**< 2D 32-bit unsigned integer type. */
	Uint3,    /**< 3D 32-bit unsigned integer type. */
	Uint4,    /**< 4D 32-bit unsigned integer type. */
	Float2,   /**< 2D 32-bit floating point type. (IEEE-754) */
	Float3,   /**< 3D 32-bit floating point type. (IEEE-754) */
	Float4,   /**< 4D 32-bit floating point type. (IEEE-754) */
	Float2x2, /**< 2x2 matrix 32-bit floating point type. (IEEE-754) */
	Float3x3, /**< 3x3 matrix 32-bit floating point type. (IEEE-754) */
	Float4x4, /**< 4x4 matrix 32-bit floating point type. (IEEE-754) */
	Float2x3, /**< 2x3 matrix 32-bit floating point type. (IEEE-754) */
	Float3x2, /**< 3x2 matrix 32-bit floating point type. (IEEE-754) */
	Float2x4, /**< 2x4 matrix 32-bit floating point type. (IEEE-754) */
	Float4x2, /**< 4x2 matrix 32-bit floating point type. (IEEE-754) */
	Float3x4, /**< 3x4 matrix 32-bit floating point type. (IEEE-754) */
	Float4x3, /**< 4x3 matrix 32-bit floating point type. (IEEE-754) */
	Count     /**< GSL data type count. */
};

/**
 * @brief GSL data formats. (Garden Shading Language)
 * @details Data formats that can be used for vertex attributes.
 */
enum class GslDataFormat : uint8
{
	F8,   /**< 8-bit normalized unsigned integer format. */
	F16,  /**< 16-bit normalized unsigned integer format. */
	F32,  /**< 32-bit floating point format. (IEEE-754) */
	I8,   /**< 8-bit signed integer format. (1 byte) */
	I16,  /**< 16-bit signed integer format. (2 bytes) */
	I32,  /**< 32-bit signed integer format. (4 bytes) */
	U8,   /**< 8-bit unsigned integer format. (1 byte) */
	U16,  /**< 16-bit unsigned integer format. (2 bytes) */
	U32,  /**< 32-bit unsigned integer format. (4 bytes) */
	Count /**< GSL data format count. */
};

/***********************************************************************************************************************
 * @brief GSL uniform types. (Garden Shading Language)
 */
enum class GslUniformType : uint8
{
	Sampler1D,             /**< 1D floating point combined image sampler. (read only access) */
	Sampler2D,             /**< 2D floating point combined image sampler. (read only access) */
	Sampler3D,             /**< 3D floating point combined image sampler. (read only access) */
	SamplerCube,           /**< Cubemap floating point combined image sampler. (read only access) */
	Sampler1DArray,        /**< 1D floating point combined image array sampler. (read only access) */
	Sampler2DArray,        /**< 2D floating point combined image array sampler. (read only access) */

	Isampler1D,            /**< 1D signed integer combined image sampler. (read only access) */
	Isampler2D,            /**< 2D signed integer combined image sampler. (read only access) */
	Isampler3D,            /**< 3D signed integer combined image sampler. (read only access) */
	IsamplerCube,          /**< Cubemap signed integer combined image sampler. (read only access) */
	Isampler1DArray,       /**< 1D signed integer combined image array sampler. (read only access) */
	Isampler2DArray,       /**< 2D signed integer combined image array sampler. (read only access) */

	Usampler1D,            /**< 1D unsigned integer combined image sampler. (read only access) */
	Usampler2D,            /**< 2D unsigned integer combined image sampler. (read only access) */
	Usampler3D,            /**< 3D unsigned integer combined image sampler. (read only access) */
	UsamplerCube,          /**< Cubemap unsigned integer combined image sampler. (read only access) */
	Usampler1DArray,       /**< 1D unsigned integer combined image array sampler. (read only access) */
	Usampler2DArray,       /**< 2D unsigned integer combined image array sampler. (read only access) */

	Sampler1DShadow,       /**< 1D depth combined image shadow sampler. (read only access) */
	Sampler2DShadow,       /**< 2D depth combined image shadow sampler. (read only access) */
	SamplerCubeShadow,     /**< Cubemap depth combined image shadow sampler. (read only access) */
	Sampler1DArrayShadow,  /**< 1D depth combined image array shadow sampler. (read only access) */
	Sampler2DArrayShadow,  /**< 2D depth combined image array shadow sampler. (read only access) */

	Image1D,               /**< 1D floating point image. (read and write access) */
	Image2D,               /**< 2D floating point image. (read and write access) */
	Image3D,               /**< 3D floating point image. (read and write access) */
	ImageCube,             /**< Cubemap floating point image. (read and write access) */
	Image1DArray,          /**< 1D floating point image array. (read and write access) */
	Image2DArray,          /**< 2D floating point image array. (read and write access) */

	Iimage1D,              /**< 1D signed integer image. (read and write access) */
	Iimage2D,              /**< 2D signed integer image. (read and write access) */
	Iimage3D,              /**< 3D signed integer image. (read and write access) */
	IimageCube,            /**< Cubemap signed integer image. (read and write access) */
	Iimage1DArray,         /**< 1D signed integer image array. (read and write access) */
	Iimage2DArray,         /**< 2D signed integer image array. (read and write access) */

	Uimage1D,              /**< 1D unsigned integer image. (read and write access) */
	Uimage2D,              /**< 2D unsigned integer image. (read and write access) */
	Uimage3D,              /**< 3D unsigned integer image. (read and write access) */
	UimageCube,            /**< Cubemap unsigned integer image. (read and write access) */
	Uimage1DArray,         /**< 1D unsigned integer image array. (read and write access) */
	Uimage2DArray,         /**< 2D unsigned integer image array. (read and write access) */

	SubpassInput,          /**< Subpass input image. (read only access) */
	UniformBuffer,         /**< Uniform buffer. (read only access) */
	StorageBuffer,         /**< Storage buffer. (read and write access) */
	PushConstants,         /**< Push constants buffer. (read only access) */
	AccelerationStructure, /**< Acceleration structure buffer. (read only access) */
	
	Count                  /**< GSL uniform type count. */
};

/***********************************************************************************************************************
 * @brief GSL data type name strings. (camelCase)
 */
constexpr string_view gslDataTypeNames[(psize)GslDataType::Count] =
{
	"bool", "int", "uint", "float", "bool2", "bool3", "bool4",
	"int2", "int3", "int4", "uint2", "uint3", "uint4",
	"float2", "float3", "float4", "float2x2", "float3x3", "float4x4",
	"float2x3", "float3x2", "float2x4", "float4x2", "float3x4", "float4x3"
};
/**
 * @brief GSL data format name strings. (camelCase)
 */
constexpr string_view gslDataFormatNames[(psize)GslDataFormat::Count] =
{
	"f8", "f16", "f32", "i8", "i16", "i32", "u8", "u16", "u32"
};
/**
 * @brief GSL uniform type name strings. (camelCase)
 */
constexpr string_view gslUniformTypeNames[(psize)GslUniformType::Count] =
{
	"sampler1D", "sampler2D", "sampler3D", "samplerCube", "sampler1DArray", "sampler2DArray",
	"isampler1D", "isampler2D", "isampler3D", "isamplerCube", "isampler1DArray", "isampler2DArray",
	"usampler1D", "usampler2D", "usampler3D", "usamplerCube", "usampler1DArray", "usampler2DArray",
	"sampler1DShadow", "sampler2DShadow", "samplerCubeShadow",
	"sampler1DArrayShadow", "sampler2DArrayShadow",
	"image1D", "image2D", "image3D", "imageCube", "image1DArray", "image2DArray",
	"iimage1D", "iimage2D", "iimage3D", "iimageCube", "iimage1DArray", "iimage2DArray",
	"uimage1D", "uimage2D", "uimage3D", "uimageCube", "uimage1DArray", "uimage2DArray",
	"subpassInput", "uniformBuffer", "storageBuffer", "pushConstants"
};

/***********************************************************************************************************************
 * @brief Returns GSL data type.
 * @param dataType target GSL data type name string (camelCase)
 * @throw GardenError on unknown GSL data type.
 */
static GslDataType toGslDataType(string_view dataType)
{
	if (dataType == "bool") return GslDataType::Bool;
	if (dataType == "int32") return GslDataType::Int32;
	if (dataType == "uint32") return GslDataType::Uint32;
	if (dataType == "float") return GslDataType::Float;
	if (dataType == "bool2") return GslDataType::Bool2;
	if (dataType == "bool3") return GslDataType::Bool3;
	if (dataType == "bool4") return GslDataType::Bool4;
	if (dataType == "int2") return GslDataType::Int2;
	if (dataType == "int3") return GslDataType::Int3;
	if (dataType == "int4") return GslDataType::Int4;
	if (dataType == "uint2") return GslDataType::Uint2;
	if (dataType == "uint3") return GslDataType::Uint3;
	if (dataType == "uint4") return GslDataType::Uint4;
	if (dataType == "float2") return GslDataType::Float2;
	if (dataType == "float3") return GslDataType::Float3;
	if (dataType == "float4") return GslDataType::Float4;
	if (dataType == "float2x2") return GslDataType::Float2x2;
	if (dataType == "float3x3") return GslDataType::Float3x3;
	if (dataType == "float4x4") return GslDataType::Float4x4;
	if (dataType == "float2x3") return GslDataType::Float2x3;
	if (dataType == "float3x2") return GslDataType::Float3x2;
	if (dataType == "float2x4") return GslDataType::Float2x4;
	if (dataType == "float4x2") return GslDataType::Float4x2;
	if (dataType == "float3x4") return GslDataType::Float3x4;
	if (dataType == "float4x3") return GslDataType::Float4x3;
	throw GardenError("Unknown GSL data type. (" + string(dataType) + ")");
}

/**
 * @brief Returns GSL data format.
 * @param dataFormat target GSL data format name string (camelCase)
 * @throw GardenError on unknown GSL data format type.
 */
static GslDataFormat toGslDataFormat(string_view dataFormat)
{
	if (dataFormat == "f8") return GslDataFormat::F8;
	if (dataFormat == "f16") return GslDataFormat::F16;
	if (dataFormat == "f32") return GslDataFormat::F32;
	if (dataFormat == "i8") return GslDataFormat::I8;
	if (dataFormat == "i16") return GslDataFormat::I16;
	if (dataFormat == "i32") return GslDataFormat::I32;
	if (dataFormat == "u8") return GslDataFormat::U8;
	if (dataFormat == "u16") return GslDataFormat::U16;
	if (dataFormat == "u32") return GslDataFormat::U32;
	throw GardenError("Unknown GSL data format type. (" + string(dataFormat) + ")");
}

/***********************************************************************************************************************
 * @brief Returns GSL uniform type.
 * @param uniformType target GSL uniform type name string (camelCase)
 * @throw GardenError on unknown GSL uniform type.
 */
static GslUniformType toGslUniformType(string_view uniformType)
{
	if (uniformType == "sampler1D") return GslUniformType::Sampler1D;
	if (uniformType == "sampler2D") return GslUniformType::Sampler2D;
	if (uniformType == "sampler3D") return GslUniformType::Sampler3D;
	if (uniformType == "samplerCube") return GslUniformType::SamplerCube;
	if (uniformType == "sampler1DArray") return GslUniformType::Sampler1DArray;
	if (uniformType == "sampler2DArray") return GslUniformType::Sampler2DArray;

	if (uniformType == "isampler1D") return GslUniformType::Isampler1D;
	if (uniformType == "isampler2D") return GslUniformType::Isampler2D;
	if (uniformType == "isampler3D") return GslUniformType::Isampler3D;
	if (uniformType == "isamplerCube") return GslUniformType::IsamplerCube;
	if (uniformType == "isampler1DArray") return GslUniformType::Isampler1DArray;
	if (uniformType == "isampler2DArray") return GslUniformType::Isampler2DArray;

	if (uniformType == "usampler1D") return GslUniformType::Usampler1D;
	if (uniformType == "usampler2D") return GslUniformType::Usampler2D;
	if (uniformType == "usampler3D") return GslUniformType::Usampler3D;
	if (uniformType == "usamplerCube") return GslUniformType::UsamplerCube;
	if (uniformType == "usampler1DArray") return GslUniformType::Usampler1DArray;
	if (uniformType == "usampler2DArray") return GslUniformType::Usampler2DArray;

	if (uniformType == "sampler1DShadow") return GslUniformType::Sampler1DShadow;
	if (uniformType == "sampler2DShadow") return GslUniformType::Sampler2DShadow;
	if (uniformType == "samplerCubeShadow") return GslUniformType::SamplerCubeShadow;
	if (uniformType == "sampler1DArrayShadow") return GslUniformType::Sampler1DArrayShadow;
	if (uniformType == "sampler2DArrayShadow") return GslUniformType::Sampler2DArrayShadow;

	if (uniformType == "image1D") return GslUniformType::Image1D;
	if (uniformType == "image2D") return GslUniformType::Image2D;
	if (uniformType == "image3D") return GslUniformType::Image3D;
	if (uniformType == "imageCube") return GslUniformType::ImageCube;
	if (uniformType == "image1DArray") return GslUniformType::Image1DArray;
	if (uniformType == "image2DArray") return GslUniformType::Image2DArray;

	if (uniformType == "iimage1D") return GslUniformType::Iimage1D;
	if (uniformType == "iimage2D") return GslUniformType::Iimage2D;
	if (uniformType == "iimage3D") return GslUniformType::Iimage3D;
	if (uniformType == "iimageCube") return GslUniformType::IimageCube;
	if (uniformType == "iimage1DArray") return GslUniformType::Iimage1DArray;
	if (uniformType == "iimage2DArray") return GslUniformType::Iimage2DArray;

	if (uniformType == "uimage1D") return GslUniformType::Uimage1D;
	if (uniformType == "uimage2D") return GslUniformType::Uimage2D;
	if (uniformType == "uimage3D") return GslUniformType::Uimage3D;
	if (uniformType == "uimageCube") return GslUniformType::UimageCube;
	if (uniformType == "uimage1DArray") return GslUniformType::Uimage1DArray;
	if (uniformType == "uimage2DArray") return GslUniformType::Uimage2DArray;

	if (uniformType == "subpassInput") return GslUniformType::SubpassInput;
	if (uniformType == "uniformBuffer") return GslUniformType::UniformBuffer;
	if (uniformType == "storageBuffer") return GslUniformType::StorageBuffer;
	if (uniformType == "pushConstants") return GslUniformType::PushConstants;
	if (uniformType == "accelerationStructure") return GslUniformType::AccelerationStructure;
	
	throw GardenError("Unknown GSL uniform type. (" + string(uniformType) + ")");
}

/***********************************************************************************************************************
 * @brief Returns GSL data type name string.
 * @param dataType target GSL data type
 */
static string_view toString(GslDataType dataType) noexcept
{
	GARDEN_ASSERT(dataType < GslDataType::Count);
	return gslDataTypeNames[(psize)dataType];
}
/**
 * @brief Returns GSL data format name string.
 * @param dataFormat target GSL data format
 */
static string_view toString(GslDataFormat dataFormat) noexcept
{
	GARDEN_ASSERT(dataFormat < GslDataFormat::Count);
	return gslDataFormatNames[(psize)dataFormat];
}
/**
 * @brief Returns GSL uniform type name string.
 * @param uniformType target GSL uniform type
 */
static string_view toString(GslUniformType uniformType) noexcept
{
	GARDEN_ASSERT(uniformType < GslUniformType::Count);
	return gslUniformTypeNames[(psize)uniformType];
}

/***********************************************************************************************************************
 * @brief Returns GSL data type component count. (1D, 2D, 3D, 2x2, 3x3...)
 * @param dataType target GSL data type
 */
static uint8 toComponentCount(GslDataType dataType) noexcept
{
	switch (dataType)
	{
	case GslDataType::Bool: return 1;
	case GslDataType::Int32: return 1;
	case GslDataType::Uint32: return 1;
	case GslDataType::Float: return 1;
	case GslDataType::Bool2: return 2;
	case GslDataType::Int2: return 2;
	case GslDataType::Uint2: return 2;
	case GslDataType::Float2: return 2;
	case GslDataType::Bool3: return 3;
	case GslDataType::Int3: return 3;
	case GslDataType::Uint3: return 3;
	case GslDataType::Float3: return 3;
	case GslDataType::Bool4: return 4;
	case GslDataType::Int4: return 4;
	case GslDataType::Uint4: return 4;
	case GslDataType::Float4: return 4;
	case GslDataType::Float2x2: return 4;
	case GslDataType::Float3x3: return 9;
	case GslDataType::Float4x4: return 16;
	case GslDataType::Float2x3: return 6;
	case GslDataType::Float3x2: return 6;
	case GslDataType::Float2x4: return 8;
	case GslDataType::Float4x2: return 8;
	case GslDataType::Float3x4: return 12;
	case GslDataType::Float4x3: return 12;
	default: abort();
	}
}

/***********************************************************************************************************************
 * @brief Returns GSL data type location offset.
 * @param dataType target GSL data type
 */
static uint8 toLocationOffset(GslDataType dataType) noexcept
{
	switch (dataType)
	{
	case GslDataType::Bool: return 1;
	case GslDataType::Int32: return 1;
	case GslDataType::Uint32: return 1;
	case GslDataType::Float: return 1;
	case GslDataType::Bool2: return 1;
	case GslDataType::Int2: return 1;
	case GslDataType::Uint2: return 1;
	case GslDataType::Float2: return 1;
	case GslDataType::Bool3: return 1;
	case GslDataType::Int3: return 1;
	case GslDataType::Uint3: return 1;
	case GslDataType::Float3: return 1;
	case GslDataType::Bool4: return 1;
	case GslDataType::Int4: return 1;
	case GslDataType::Uint4: return 1;
	case GslDataType::Float4: return 1;
	case GslDataType::Float2x2: return 2;
	case GslDataType::Float3x3: return 3;
	case GslDataType::Float4x4: return 4;
	case GslDataType::Float2x3: return 3;
	case GslDataType::Float3x2: return 2;
	case GslDataType::Float2x4: return 4;
	case GslDataType::Float4x2: return 2;
	case GslDataType::Float3x4: return 4;
	case GslDataType::Float4x3: return 3;
	default: abort();
	}
}

/***********************************************************************************************************************
 * @brief Returns GSL data type binary size in bytes.
 * @param dataType target GSL data type
 */
static constexpr psize toBinarySize(GslDataType dataType) noexcept
{
	switch (dataType)
	{
	case GslDataType::Bool: return sizeof(uint32);
	case GslDataType::Int32: return sizeof(int32);
	case GslDataType::Uint32: return sizeof(uint32);
	case GslDataType::Float: return sizeof(float);
	case GslDataType::Bool2: return sizeof(uint32) * 2;
	case GslDataType::Bool3: return sizeof(uint32) * 3;
	case GslDataType::Bool4: return sizeof(uint32) * 4;
	case GslDataType::Int2: return sizeof(int32) * 2;
	case GslDataType::Int3: return sizeof(int32) * 3;
	case GslDataType::Int4: return sizeof(int32) * 4;
	case GslDataType::Uint2: return sizeof(uint32) * 2;
	case GslDataType::Uint3: return sizeof(uint32) * 3;
	case GslDataType::Uint4: return sizeof(uint32) * 4;
	case GslDataType::Float2: return sizeof(float) * 2;
	case GslDataType::Float3: return sizeof(float) * 3;
	case GslDataType::Float4: return sizeof(float) * 4;
	case GslDataType::Float2x2: return sizeof(float) * 2 * 2;
	case GslDataType::Float3x3: return sizeof(float) * 3 * 3;
	case GslDataType::Float4x4: return sizeof(float) * 4 * 4;
	case GslDataType::Float2x3: return sizeof(float) * 2 * 3;
	case GslDataType::Float3x2: return sizeof(float) * 3 * 2;
	case GslDataType::Float2x4: return sizeof(float) * 2 * 4;
	case GslDataType::Float4x2: return sizeof(float) * 4 * 2;
	case GslDataType::Float3x4: return sizeof(float) * 3 * 4;
	case GslDataType::Float4x3: return sizeof(float) * 4 * 3;
	default: return 0;
	}
}
/**
 * @brief Returns GSL data format binary size in bytes.
 * @param dataFormat target GSL data format
 */
static constexpr psize toBinarySize(GslDataFormat dataFormat) noexcept
{
	switch (dataFormat)
	{
	case GslDataFormat::F8: return sizeof(uint8);
	case GslDataFormat::F16: return sizeof(uint16);
	case GslDataFormat::F32: return sizeof(float);
	case GslDataFormat::I8: return sizeof(int8);
	case GslDataFormat::I16: return sizeof(int16);
	case GslDataFormat::I32: return sizeof(int32);
	case GslDataFormat::U8: return sizeof(uint8);
	case GslDataFormat::U16: return sizeof(uint16);
	case GslDataFormat::U32: return sizeof(uint32);
	default: return 0;
	}
}

/***********************************************************************************************************************
 * @brief Is the GSL uniform type a sampler.
 * @param uniformType target GSL uniform type
 */
static constexpr bool isSamplerType(GslUniformType uniformType) noexcept
{
	return GslUniformType::Sampler1D <= uniformType && uniformType <= GslUniformType::Sampler2DArrayShadow;
}
/**
 * @brief Is the GSL uniform type an image.
 * @param uniformType target GSL uniform type
 */
static constexpr bool isImageType(GslUniformType uniformType) noexcept
{
	return GslUniformType::Image1D <= uniformType && uniformType <= GslUniformType::Uimage2DArray;
}
/**
 * @brief Is the GSL uniform type a buffer.
 * @param uniformType target GSL uniform type
 */
static constexpr bool isBufferType(GslUniformType uniformType) noexcept
{
	return uniformType == GslUniformType::UniformBuffer || uniformType == GslUniformType::StorageBuffer;
}

} // namespace garden::graphics
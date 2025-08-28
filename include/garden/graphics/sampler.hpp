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
 * @brief Common graphics sampler functions.
 */

#pragma once
#include "linear-pool.hpp"
#include "garden/graphics/resource.hpp"
#include <cmath>

namespace garden::graphics
{

using namespace ecsm;
class SamplerExt;

/**
 * @brief Graphics shader image sampler.
 * 
 * @details 
 * Object that controls how textures are read when applied to 3D surfaces. It defines parameters such as filtering, 
 * addressing modes, LOD (Level of Detail) bias, etc. ensuring that textures are sampled efficiently and appear 
 * correctly at different distances and angles. Essentially, it acts as the intermediary between the raw texture data 
 * and the shader programs that use this data to produce visual effects.
 */
class Sampler final : public Resource
{
public:
	/*******************************************************************************************************************
	 * @brief Texture (image) sampling method type.
	 * 
	 * @details
	 * Method used by the GPU to determine the color of a texture sample based on the texture coordinates 
	 * provided by a shader. When a texture is sampled in a shader, the texture coordinates might not 
	 * directly correspond to the texel (texture pixel) positions in the texture. The sampler filter 
	 * type defines how the color value for these coordinates is calculated, affecting the final appearance 
	 * of the texture when it's applied to a surface in 3D space. The choice of filter type can 
	 * significantly impact both the visual quality and the performance of texture sampling.
	 */
	enum class Filter : uint8
	{
		Nearest, /**< Selects the color of the closest texel. (nearest-neighbour) */
		Linear,  /**< Computed by linearly interpolating between the colors of adjacent texels. */
		Count    /**< Sampler filter type count. */
	};
	/**
	 * @brief Texture sampler addressing mode.
	 * 
	 * @details
	 * Setting that determines how a texture is applied (or "sampled") when texture coordinates (also known as UV 
	 * coordinates) fall outside the standard range of [0, 1]. This scenario frequently occurs due to texture 
	 * mapping or repeating textures across a surface. The address mode defines how the GPU handles these 
	 * out-of-range texture coordinates, affecting the appearance of textured objects in 3D scenes.
	 */
	enum class AddressMode : uint8
	{
		Repeat,            /**< i = ​i % size */
		MirroredRepeat,    /**< i = (size - 1) - mirror((i % (2 * size)) - size) */
		ClampToEdge,       /**< i = clamp(i, 0, size - 1) */
		ClampToBorder,     /**< i = clamp(i, -1, size) */
		MirrorClampToEdge, /**< i = clamp(mirror(i), 0, size - 1) */
		Count              /**< Sampler address mode count. */
	};
	/**
	 * @brief Clamp to border sampling color.
	 * 
	 * @details
	 * Color applied to pixels that fall outside the texture coordinates when using @ref ClampToBorder sampling modes.
	 * 
	 * @todo Support custom color extension?
	 */
	enum class BorderColor : uint8
	{
		FloatTransparentBlack, /**< Transparent, floating point format, black color. */
		IntTransparentBlack,   /**< transparent, integer format, black color. */
		FloatOpaqueBlack,      /**< Opaque, floating point format, black color. */
		IntOpaqueBlack,        /**< Opaque, integer format, black color. */
		FloatOpaqueWhite,      /**< Opaque, floating point format, white color. */
		IntOpaqueWhite,        /**< Opaque, integer format, white color. */
		Count                  /**< Clamp to border sampling color count. */
	};
	/**
	 * @brief Comparison operator for depth, stencil, and sampler operations
	 * 
	 * @details
	 * Used to compare two values against each other. These operators are fundamental in various graphics operations, 
	 * such as depth testing, stencil testing, and shadow mapping, where decisions are made based on comparing 
	 * values like depth (Z) values of fragments or stencil buffer values.
	 */
	enum class CompareOp : uint8
	{
		Never,          /**< Comparison always evaluates false. */
		Less,           /**< Comparison evaluates reference < test. */
		Equal,          /**< Comparison evaluates reference == test. */
		LessOrEqual,    /**< Comparison evaluates reference <= test. */
		Greater,        /**< Comparison evaluates reference > test. */
		NotEqual,       /**< Comparison evaluates reference != test. */
		GreaterOrEqual, /**< Comparison evaluates reference >= test. */
		Always,         /**< Comparison always evaluates true. */
		Count           /**< Comparison operator type count. */
	};

	/*******************************************************************************************************************
	 * @brief Sampler configuration.
	 * 
	 * @details
	 * Configuration used to determine how a texture is sampled when applied to a 3D model. The process of mapping 
	 * these textures onto the 3D surfaces is called texture mapping, and sampling is a crucial part of this process. 
	 * The sampler state defines how the texture is accessed and filtered when it is being applied to a model.
	 */
	struct State final
	{
		uint8 anisoFiltering : 1;                                     /**< Is anisotropic filtering enabled. */
		uint8 comparison : 1;                                         /**< Is comparison during lookups enabled. */
		uint8 unnormCoords : 1;                                       /**< Is unnormalized coordinates enabled. */
		uint8 _unused : 5;                                            /**< [reserved for future use] */
		Filter minFilter = Filter::Nearest;                           /**< Minification filter to apply to lookups. */
		Filter magFilter = Filter::Nearest;                           /**< Magnification filter to apply to lookups. */
		Filter mipmapFilter = Filter::Nearest;                        /**< Mipmap filter to apply to lookups. */
		AddressMode addressModeX = AddressMode::ClampToEdge;            /**< Addressing mode for U coordinates outside [0,1) */
		AddressMode addressModeY = AddressMode::ClampToEdge;            /**< Addressing mode for V coordinates outside [0,1) */
		AddressMode addressModeZ = AddressMode::ClampToEdge;            /**< Addressing mode for W coordinates outside [0,1) */
		CompareOp compareOperation = CompareOp::Less;                 /**< Comparison operator to apply to fetched data/ */
		float maxAnisotropy = 1.0f;                                   /**< Anisotropy value clamp used by the sampler. */
		float mipLodBias = 0.0f;                                      /**< Bias to be added to mipmap LOD calculation. */
		float minLod = 0.0f;                                          /**< Used to clamp the minimum of the computed LOD value. */
		float maxLod = INFINITY;                                      /**< Used to clamp the maximum of the computed LOD value. */
		BorderColor borderColor = BorderColor::FloatTransparentBlack; /**< Predefined border color to use. */
		uint8 _alignment0 = 0;                                        /**< [structure alignment] */
		uint16 _alignment1 = 0;                                       /**< [structure alignment] */
		// should be aligned.
 
		/**
		 * @brief Creates a new default sampler state.
		 */
		State() : anisoFiltering(0), comparison(0), unnormCoords(0), _unused(0) { }
		
		/**
		 * @brief Sets sampler minification, magnification and mipmap filter type.
		 */
		void setFilter(Filter filter) noexcept { minFilter = magFilter = mipmapFilter = filter; }
		/**
		 * @brief Sets sampler U, V and W coordinates addressing mode.
		 */
		void setAddressMode(AddressMode mode) noexcept { addressModeX = addressModeY = addressModeZ = mode; }
	};
private:
	State state = {};

	Sampler(const State& state);
	bool destroy() final;

	friend class SamplerExt;
	friend class LinearPool<Sampler>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty sampler data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access samplers.
	 */
	Sampler() = default;

	/**
	 * @brief Returns sampler state.
	 */
	State getState() const noexcept { return state; }

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets sampler debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif
};

/**
 * @brief Returns sampler address mode.
 * @param addressMode target sampler address mode name string (camelCase)
 * @throw GardenError on unknown sampler address mode.
 */
static Sampler::AddressMode toAddressMode(string_view addressMode)
{
	if (addressMode == "repeat") return Sampler::AddressMode::Repeat;
	if (addressMode == "mirroredRepeat") return Sampler::AddressMode::MirroredRepeat;
	if (addressMode == "clampToEdge") return Sampler::AddressMode::ClampToEdge;
	if (addressMode == "clampToBorder") return Sampler::AddressMode::ClampToBorder;
	if (addressMode == "mirrorClampToEdge") return Sampler::AddressMode::MirrorClampToEdge;
	throw GardenError("Unknown sampler address mode. (" + string(addressMode) + ")");
}
/**
 * @brief Returns border color type.
 * @param borderColor target border color name string (camelCase)
 * @throw GardenError on unknown border color type.
 */
static Sampler::BorderColor toBorderColor(string_view borderColor)
{
	if (borderColor == "floatTransparentBlack") return Sampler::BorderColor::FloatTransparentBlack;
	if (borderColor == "intTransparentBlack") return Sampler::BorderColor::IntTransparentBlack;
	if (borderColor == "floatOpaqueBlack") return Sampler::BorderColor::FloatOpaqueBlack;
	if (borderColor == "intOpaqueBlack") return Sampler::BorderColor::IntOpaqueBlack;
	if (borderColor == "floatOpaqueWhite") return Sampler::BorderColor::FloatOpaqueWhite;
	if (borderColor == "intOpaqueWhite") return Sampler::BorderColor::IntOpaqueWhite;
	throw GardenError("Unknown border color type. (" + string(borderColor) + ")");
}
/**
 * @brief Returns comparison operator type.
 * @param compareOperation target comparison operator name string (camelCase)
 * @throw GardenError on unknown comparison operator type.
 */
static Sampler::CompareOp toCompareOperation(string_view compareOperation)
{
	if (compareOperation == "never") return Sampler::CompareOp::Never;
	if (compareOperation == "less") return Sampler::CompareOp::Less;
	if (compareOperation == "equal") return Sampler::CompareOp::Equal;
	if (compareOperation == "lessOrEqual") return Sampler::CompareOp::LessOrEqual;
	if (compareOperation == "greater") return Sampler::CompareOp::Greater;
	if (compareOperation == "notEqual") return Sampler::CompareOp::NotEqual;
	if (compareOperation == "greaterOrEqual") return Sampler::CompareOp::GreaterOrEqual;
	if (compareOperation == "always") return Sampler::CompareOp::Always;
	throw GardenError("Unknown compare operation type. (" + string(compareOperation) + ")");
}

/***********************************************************************************************************************
 * @brief Sampler filter name strings.
 */
constexpr const char* samplerFilterNames[(psize)Sampler::Filter::Count] =
{
	"Nearest", "Linear"
};
/**
 * @brief Sampler address mode name strings.
 */
constexpr const char* addressModeNames[(psize)Sampler::AddressMode::Count] =
{
	"Repeat", "MirroredRepeat", "ClampToEdge", "ClampToBorder", "MirrorClampToEdge"
};
/**
 * @brief Sampler border color name strings.
 */
constexpr const char* borderColorNames[(psize)Sampler::BorderColor::Count] =
{
	"FloatTransparentBlack", "IntTransparentBlack", "FloatOpaqueBlack",
	"IntOpaqueBlack", "FloatOpaqueWhite", "IntOpaqueWhite"
};
/**
 * @brief Sampler compare operation name strings.
 */
constexpr const char* compareOperationNames[(psize)Sampler::CompareOp::Count] =
{
	"Never", "Less", "Equal", "LessOrEqual", "Greater", "NotEqual", "GreaterOrEqual", "Always"
};

/**
 * @brief Returns sampler filter type.
 * @param samplerFilter target sampler filter name (camelCase)
 * @throw GardenError on unknown sampler filter type.
 */
static Sampler::Filter toSamplerFilter(string_view samplerFilter)
{
	if (samplerFilter == "nearest") return Sampler::Filter::Nearest;
	if (samplerFilter == "linear") return Sampler::Filter::Linear;
	throw GardenError("Unknown sampler filter type. (" + string(samplerFilter) + ")");
}
/**
 * @brief Returns sampler filter name string.
 * @param samplerFilter target sampler filter type
 */
static string_view toString(Sampler::Filter samplerFilter) noexcept
{
	GARDEN_ASSERT(samplerFilter < Sampler::Filter::Count);
	return samplerFilterNames[(psize)samplerFilter];
}
/**
 * @brief Returns sampler address mode name string.
 * @param memoryAccess target sampler address mode
 */
static string_view toString(Sampler::AddressMode addressMode) noexcept
{
	GARDEN_ASSERT(addressMode < Sampler::AddressMode::Count);
	return addressModeNames[(psize)addressMode];
}
/**
 * @brief Returns border color name string.
 * @param memoryAccess target border color type
 */
static string_view toString(Sampler::BorderColor borderColor) noexcept
{
	GARDEN_ASSERT(borderColor < Sampler::BorderColor::Count);
	return borderColorNames[(psize)borderColor];
}
/**
 * @brief Returns comparison operator name string.
 * @param memoryAccess target comparison operator type
 */
static string_view toString(Sampler::CompareOp compareOperation) noexcept
{
	GARDEN_ASSERT(compareOperation < Sampler::CompareOp::Count);
	return compareOperationNames[(psize)compareOperation];
}

/***********************************************************************************************************************
 * @brief Graphics sampler resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class SamplerExt final
{
public:
	/**
	 * @brief Returns sampler state.
	 * @warning In most cases you should use @ref Sampler functions.
	 * @param[in] sampler target sampler instance
	 */
	static Sampler::State& getState(Sampler& sampler) noexcept { return sampler.state; }
};

} // namespace garden::graphics
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
 * @brief Common graphics image (texture) functions.
 */

#pragma once
#include "garden/graphics/gsl.hpp"
#include "garden/graphics/buffer.hpp"
#include "garden/graphics/sampler.hpp"

namespace garden::graphics
{

class ImageView;
class ImageExt;
class ImageViewExt;

/**
 * @brief Graphics image (texture) storage.
 * 
 * @details 
 * A structured collection of data designed to store multidimensional arrays of pixels or texels (texture elements). 
 * Images are used for a wide range of purposes, including textures for 3D models, render targets for off-screen 
 * rendering, and as resources for various image-based operations like image processing or post-processing effects.
 */
class Image final : public Memory
{
public:
	/**
	 * @brief Image dimensionality type.
	 * 
	 * @details
	 * Impacts how the image is allocated and used within the GPU, as well as how 
	 * shaders sample data from the image. The choice between 1D, 2D and 3D images 
	 * depends on the specific requirements of the application, such as the 
	 * nature of the textures being used, the desired effects, and the 
	 * performance considerations of the rendering pipeline.
	 */
	enum class Type : uint8
	{
		Texture1D,      /**< One-dimensional image. */
		Texture2D,      /**< Two-dimensional image. */
		Texture3D,      /**< Three-dimensional image. */
		Texture1DArray, /**< One-dimensional image array. */
		Texture2DArray, /**< Two-dimensional image array. */
		Cubemap,        /**< Texture with six faces. */
		Count           /**< Image dimensionality type count. */
	};
	/*******************************************************************************************************************
	 * @brief Image data format.
	 * 
	 * @details
	 * These formats determine how the data for each pixel in an image is 
	 * arranged, including the number of color components, the bit depth of 
	 * each component, and whether the data is compressed or uncompressed.
	 * 
	 * Image format identification:
	 * Sfloat  - signed floating point (0.0, -1.0, 2.22, -50.5, ...)
	 * Ufloat  - unsigned floating point (0.0, 1.0, 1.23, 10.0, ...)
	 * Sint    - signed integer (0, 1, 5, 32, ...)
	 * Uint    - unsigned integer (0, -2, 40, -12, ...)
	 * Unorm   - normalized uint as float [0.0, 1.0] (255 -> 1.0)
	 * Snorm   - normalized int as float [-1.0, 1.0] (0 -> -1.0)
	 * Uscaled - scaled uint as float (128 -> 128.0)
	 * Sscaled - scaled int as float (-32 -> -32.0)
	 * Srgb    - sRGB color space uint (0, 1, 32, 255, ...)
	 */
	enum class Format : uint8
	{
		Undefined,          /**< Undefined image data format. */

		UintR8,             /**< 8-bit unsigned integer (red only channel) format. */
		UintR8G8,           /**< 8-bit unsigned integer (red and green channel) format. */
		UintR8G8B8A8,       /**< 8-bit unsigned integer (red, green, blue, alpha channel) format. */
		UintR16,            /**< 16-bit unsigned integer (red only channel) format. */
		UintR16G16,         /**< 16-bit unsigned integer (red and green channel) format. */
		UintR16G16B16A16,   /**< 16-bit unsigned integer (red, green, blue, alpha channel) format. */
		UintR32,            /**< 32-bit unsigned integer (red only channel) format. */
		UintR32G32,         /**< 32-bit unsigned integer (red and green channel) format. */
		UintR32G32B32A32,   /**< 32-bit unsigned integer (red, green, blue, alpha channel) format. */
		UintA2R10G10B10,    /**< unsigned integer (2-bit alpha, 10-bit red/green/blue channel) format. */
		UintA2B10G10R10,    /**< unsigned integer (2-bit alpha, 10-bit blue/green/red channel) format. */

		SintR8,             /**< 8-bit signed integer (red only channel) format. */
		SintR8G8,           /**< 8-bit signed integer (red and green channel) format. */
		SintR8G8B8A8,       /**< 8-bit signed integer (red, green, blue, alpha channel) format. */
		SintR16,            /**< 16-bit signed integer (red only channel) format. */
		SintR16G16,         /**< 16-bit signed integer (red and green channel) format. */
		SintR16G16B16A16,   /**< 16-bit signed integer (red, green, blue, alpha channel) format. */
		SintR32,            /**< 32-bit signed integer (red only channel) format. */
		SintR32G32,         /**< 32-bit signed integer (red and green channel) format. */
		SintR32G32B32A32,   /**< 32-bit signed integer (red, green, blue, alpha channel) format. */

		UnormR8,            /**< 8-bit normalized uint as float (red only channel) format. */
		UnormR8G8,          /**< 8-bit normalized uint as float (red and green channel) format. */
		UnormR8G8B8A8,      /**< 8-bit normalized uint as float (red, green, blue, alpha channel) format. */
		UnormB8G8R8A8,      /**< 8-bit normalized uint as float (blue, green, red, alpha channel) format. */
		UnormR16,           /**< 16-bit normalized uint as float (red only channel) format. */
		UnormR16G16,        /**< 16-bit normalized uint as float (red and green channel) format. */
		UnormR16G16B16A16,  /**< 16-bit normalized uint as float (red, green, blue, alpha channel) format. */
		UnormR5G6B5,        /**< normalized uint as float (5-bit red, 6-bit green, 5-bit blue channel) format. */
		UnormA1R5G5B5,      /**< normalized uint as float (1-bit alpha, 5-bit red/green/blue channel) format. */
		UnormR5G5B5A1,      /**< normalized uint as float (5-bit red/green/blue channel, 1-bit alpha) format. */
		UnormB5G5R5A1,      /**< normalized uint as float (5-bit blue/green/red channel, 1-bit alpha) format. */
		UnormR4G4B4A4,      /**< normalized uint as float (4-bit red/green/blue/alpha channel) format. */
		UnormB4G4R4A4,      /**< normalized uint as float (4-bit blue/green/red/alpha channel) format. */
		UnormA2R10G10B10,   /**< normalized uint as float (2-bit alpha, 10-bit red/green/blue channel) format. */
		UnormA2B10G10R10,   /**< normalized uint as float (2-bit alpha, 10-bit blue/green/red channel) format. */

		SnormR8,            /**< 8-bit normalized uint as float (red only channel) format. */
		SnormR8G8,          /**< 8-bit normalized uint as float (red and green channel) format. */
		SnormR8G8B8A8,      /**< 8-bit normalized uint as float (red, green, blue, alpha channel) format. */
		SnormR16,           /**< 16-bit normalized uint as float (red only channel) format. */
		SnormR16G16,        /**< 16-bit normalized uint as float (red and green channel) format. */
		SnormR16G16B16A16,  /**< 16-bit normalized uint as float (red, green, blue, alpha channel) format. */

		SfloatR16,          /**< 16-bit signed floating point (red only channel) format. */
		SfloatR16G16,       /**< 16-bit signed floating point (red and green channel) format. */
		SfloatR16G16B16A16, /**< 16-bit signed floating point (red, green, blue, alpha channel) format. */
		SfloatR32,          /**< 32-bit signed floating point (red only channel) format. */
		SfloatR32G32,       /**< 32-bit signed floating point (red and green channel) format. */
		SfloatR32G32B32A32, /**< 32-bit signed floating point (red, green, blue, alpha channel) format. */

		UfloatB10G11R11,    /**< Unsigned floating point (10-bit blue, 11-bit green, 10-bit red channel) format. */
		UfloatE5B9G9R9,     /**< Unsigned floating point (5-bit exponent, 9-bit blue/green/red channel) format. */

		SrgbR8G8B8A8,       /**< 8-bit sRGB color space (red, green, blue, alpha channel) format. */
		SrgbB8G8R8A8,       /**< 8-bit sRGB color space (blue, green, red, alpha channel) format. */

		UnormD16,           /**< 16-bit normalized uint as float depth format. */
		SfloatD32,          /**< 32-bit signed floating point depth format. */
		UnormD24UintS8,     /**< 24-bit normalized uint as float depth and 8-bit unsigned integer stencil format. */
		SfloatD32Uint8S,    /**< 32-bit signed floating depth and 8-bit unsigned integer stencil format. */

		Count               /**< Image data format count. */
		// TODO: A8B8G8R8
	};
	/**
	 * @brief Image bind type. (Affects driver optimizations)
	 * 
	 * @details
	 * Image bind flags are critical for ensuring that an image is compatible 
	 * with the operations the application intends to perform on it.
	 */
	enum class Bind : uint8
	{
		None                   = 0x00, /**< No image usage specified, zero mask. */
		TransferSrc            = 0x01, /**< Image can be used as the source of a transfer command. */
		TransferDst            = 0x02, /**< Image can be used as the destination of a transfer command. */
		Sampled                = 0x04, /**< Image can be used in a descriptor set. */
		Storage                = 0x08, /**< Image can be used in a descriptor set. */
		ColorAttachment        = 0x10, /**< Image can be used as the framebuffer color attachment. */
		DepthStencilAttachment = 0x20, /**< Image can be used as the framebuffer depth or/and stencil attachment. */
		InputAttachment        = 0x40, /**< Image can be used as the framebuffer subpass input attachment. */
		Fullscreen             = 0x80, /**< Image will be the size of the window or larger. (Better optimization) */
	};

	/*******************************************************************************************************************
	 * @brief Image clear region description.
	 * @details See the @ref Image::clear().
	 */
	struct ClearRegion final
	{
		uint32 baseMip = 0;    /**< Base mipmap level. */
		uint32 mipCount = 0;   /**< Mipmap level count. */
		uint32 baseLayer = 0;  /**< Base array layer. */
		uint32 layerCount = 0; /**< Array layer count. */
	};
	/**
	 * @brief Image copy region description.
	 * @details See the @ref Image::copy().
	 */
	struct CopyImageRegion final
	{
		uint3 srcOffset = uint3::zero; /**< Source image region offset in texels. */
		uint3 dstOffset = uint3::zero; /**< Destination image region offset in texels. */
		uint3 extent = uint3::zero;    /**< Copy region extent in texels. */
		uint32 srcBaseLayer = 0;       /**< Source image base array layer. */
		uint32 dstBaseLayer = 0;       /**< Destination image base array layer. */
		uint32 layerCount = 0;         /**< Copy array layer count. */
		uint32 srcMipLevel = 0;        /**< Source image mipmap level. */
		uint32 dstMipLevel = 0;        /**< Destination image mipmap level. */
	};
	/**
	 * @brief Image to/from buffer copy region description.
	 * @details See the @ref Image::copy().
	 */
	struct CopyBufferRegion final
	{
		uint64 bufferOffset = 0;         /**< Buffer offset in bytes. */
		uint32 bufferRowLength = 0;      /**< Buffer row length in texels. */
		uint32 bufferImageHeight = 0;    /**< Buffer image height in texels. */
		uint3 imageOffset = uint3::zero; /**< Image offset in texels. */
		uint3 imageExtent = uint3::zero; /**< Image extent in texels. */
		uint32 imageBaseLayer = 0;       /**< Image base array layer. */
		uint32 imageLayerCount = 0;      /**< Image array layer count. */
		uint32 imageMipLevel = 0;        /**< Image mipmap level. */
	};
	/**
	 * @brief Image blit region description.
	 * @details See the @ref Image::blit().
	 */
	struct BlitRegion final
	{
		uint3 srcOffset = uint3::zero; /**< Source image offset in texels. */
		uint3 srcExtent = uint3::zero; /**< Source image extent in texels. */
		uint3 dstOffset = uint3::zero; /**< Destination image offset in texels. */
		uint3 dstExtent = uint3::zero; /**< Destination image extent in texels. */
		uint32 srcBaseLayer = 0;       /**< Source image base array layer. */
		uint32 dstBaseLayer = 0;       /**< Destination image base array layer. */
		uint32 layerCount = 0;         /**< Blit array layer count. */
		uint32 srcMipLevel = 0;        /**< Source image mipmap level. */
		uint32 dstMipLevel = 0;        /**< Destination image mipmap level. */
	};

	using Layers = vector<const void*>;
	using Mips = vector<Layers>;
private:
	Type type = {};
	Format format = {};
	Bind bind = {};
	bool swapchain = false;
	uint8 _alignment = 0;
	ID<ImageView> defaultView = {};
	u32x4 size = u32x4::zero;
	vector<uint32> layouts;

	Image(Type type, Format format, Bind bind, Strategy strategy, u32x4 size, uint64 version);
	Image(Bind bind, Strategy strategy, uint64 version) noexcept :
		Memory(0, Access::None, Usage::Auto, strategy, version), bind(bind) { }
	Image(void* instance, Format format, Bind bind, Strategy strategy, uint2 size, uint64 version);
	bool destroy() final;

	friend class ImageExt;
	friend class LinearPool<Image>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty image data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access images.
	 */
	Image() = default;

	/**
	 * @brief Returns image size in texels.
	 * @details Unused image size dimensions always have size of 1.
	 */
	u32x4 getSize() const noexcept { return size; }
	/**
	 * @brief Returns image dimensionality type.
	 * @details Informs the API about how to interpret the image data in memory.
	 */
	Type getType() const noexcept { return type; }
	/**
	 * @brief Returns image data format.
	 * @details Specifies the format of pixel data in an image.
	 */
	Format getFormat() const noexcept { return format; }
	/**
	 * @brief Returns image bind type.
	 * @details Image bind type helps to optimize it usage inside the driver.
	 */
	Bind getBind() const noexcept { return bind; }
	/**
	 * @brief Returns image array layer count.
	 * @details Each layer is an individual texture having the same size and format.
	 */
	uint32 getLayerCount() const noexcept { return size.getZ(); }
	/**
	 * @brief Returns image mipmap level count.
	 * @details Number of different resolution versions of a texture that are stored in a mipmap chain.
	 */
	uint8 getMipCount() const noexcept { return (uint8)size.getW(); }
	/**
	 * @brief Is this image part of the swapchain.
	 * @details Swapchain images are provided by the graphics API.
	 */
	bool isSwapchain() const noexcept { return swapchain; }

	/**
	 * @brief Returns image default view instance.
	 * @note Creates a new image view on a first call.
	 */
	ID<ImageView> getDefaultView();
	/**
	 * @brief Does image have a default view instance.
	 * @note Default image view instance is created on a first getter call.
	 */
	bool hasDefaultView() const noexcept { return (bool)defaultView; }

	/**
	 * @brief Are specified image properties supported by the GPU.
	 * 
	 * @param type image dimensionality
	 * @param format image data format
	 * @param bind image bind type
	 * @param size image size in texels
	 * @param mipCount image mip level count
	 * @param layerCount image array layer count
	 */
	static bool isSupported(Type type, Format format, Bind bind, 
		uint3 size, uint8 mipCount = 1, uint32 layerCount = 1);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets image debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Generates image mipmap levels.
	 * @details Records image downsampling blit commands with specified filter. 
	 * @param filter blitting operation filter
	 */
	void generateMips(Sampler::Filter filter = Sampler::Filter::Linear);

	/**
	 * @brief Clears image regions with specified color.
	 * 
	 * @details
	 * Operation used to set all texels in an image to a constant value, effectively clearing or resetting the image. 
	 * This operation is commonly used at the beginning of a rendering pass to prepare the render targets for new 
	 * content, ensuring that no residual data from previous frames affects the current rendering process.
	 * 
	 * @param color floating point color for clearing
	 * @param[in] regions target image regions
	 * @param count region array size
	 */
	void clear(f32x4 color, const ClearRegion* regions, uint32 count);
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color signed integer color for clearing
	 * @param[in] regions target image regions
	 * @param count region array size
	 */
	void clear(i32x4 color, const ClearRegion* regions, uint32 count);
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 *
	 * @param color unsigned integer color for clearing
	 * @param[in] regions target image regions
	 * @param count region array size
	 */
	void clear(u32x4 color, const ClearRegion* regions, uint32 count);
	/**
	 * @brief Clears image regions with specified depth/stencil value.
	 * @details See the @ref Image::clear().
	 * 
	 * @param depth depth value for clearing
	 * @param stencil stencil value for clearing
	 * @param[in] regions target image regions
	 * @param count region array size
	 */
	void clear(float depth, uint32 stencil, const ClearRegion* regions, uint32 count);

	/*******************************************************************************************************************
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @tparam N region array size
	 * @param color floating point color for clearing
	 * @param[in] regions target image region array
	 */
	template<psize N>
	void clear(f32x4 color, const array<ClearRegion, N>& regions)
	{ clear(color, regions.data(), (uint32)N); }
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @tparam N region array size
	 * @param color signed integer color for clearing
	 * @param[in] regions target image region array
	 */
	template<psize N>
	void clear(i32x4 color, const array<ClearRegion, N>& regions)
	{ clear(color, regions.data(), (uint32)N); }
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @tparam N region array size
	 * @param color unsigned integer color for clearing
	 * @param[in] regions target image region array
	 */
	template<psize N>
	void clear(u32x4 color, const array<ClearRegion, N>& regions)
	{ clear(color, regions.data(), (uint32)N); }
	/**
	 * @brief Clears image regions with specified depth/stencil value.
	 * @details See the @ref Image::clear().
	 * 
	 * @tparam N region array size
	 * @param depth depth value for clearing
	 * @param stencil stencil value for clearing
	 * @param[in] regions target image region array
	 */
	template<psize N>
	void clear(float depth, uint32 stencil, const array<ClearRegion, N>& regions)
	{ clear(depth, stencil, regions.data(), (uint32)N); }

	/*******************************************************************************************************************
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color floating point color for clearing
	 * @param[in] regions target image region vector
	 */
	void clear(f32x4 color, const vector<ClearRegion>& regions)
	{ clear(color, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color signed integer color for clearing
	 * @param[in] regions target image region vector
	 */
	void clear(i32x4 color, const vector<ClearRegion>& regions)
	{ clear(color, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Clears image regions with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color unsigned integer color for clearing
	 * @param[in] regions target image region vector
	 */
	void clear(u32x4 color, const vector<ClearRegion>& regions)
	{ clear(color, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Clears image regions with specified depth/stencil value.
	 * @details See the @ref Image::clear().
	 * 
	 * @param depth depth value for clearing
	 * @param stencil stencil value for clearing
	 * @param[in] regions target image region vector
	 */
	void clear(float depth, uint32 stencil, const vector<ClearRegion>& regions)
	{ clear(depth, stencil, regions.data(), (uint32)regions.size()); }

	/*******************************************************************************************************************
	 * @brief Clears image region with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color floating point color for clearing
	 * @param[in] region target image region
	 */
	void clear(f32x4 color, const ClearRegion& region) { clear(color, &region, 1); }
	/**
	 * @brief Clears image region with specified color.
	 * @details See the @ref Image::clear().
	 * 
	 * @param color singed integer color for clearing
	 * @param[in] region target image region
	 */
	void clear(i32x4 color, const ClearRegion& region) { clear(color, &region, 1); }
	/**
	 * @brief Clears image region with specified color.
	 * @details See the @ref Image::clear().
	 *
	 * @param color unsigned integer color for clearing
	 * @param[in] region target image region
	 */
	void clear(u32x4 color, const ClearRegion& region) { clear(color, &region, 1); }
	/**
	 * @brief Clears image region with specified depth/stencil value.
	 * @details See the @ref Image::clear().
	 * 
	 * @param depth depth value for clearing
	 * @param stencil stencil value for clearing
	 * @param[in] region target image region
	 */
	void clear(float depth, uint32 stencil, const ClearRegion& region) { clear(depth, stencil, &region, 1); }

	/**
	 * @brief Clears entire image with specified color.
	 * @details See the @ref Image::clear().
	 * @param color floating point value for clearing
	 */
	void clear(f32x4 color) { ClearRegion region; clear(color, &region, 1); }
	/**
	 * @brief Clears entire image with specified color.
	 * @details See the @ref Image::clear().
	 * @param color signed integer value for clearing
	 */
	void clear(i32x4 color) { ClearRegion region; clear(color, &region, 1); }
	/**
	 * @brief Clears entire image with specified color.
	 * @details See the @ref Image::clear().
	 * @param color unsigned integer value for clearing
	 */
	void clear(u32x4 color) { ClearRegion region; clear(color, &region, 1); }
	/**
	 * @brief Clears entire image with specified depth/stencil value.
	 * @details See the @ref Image::clear().
	 * 
	 * @param depth depth value for clearing
	 * @param stencil stencil value for clearing
	 */
	void clear(float depth, uint32 stencil) { ClearRegion region; clear(depth, stencil, &region, 1); }

	/*******************************************************************************************************************
	 * @brief Copies data regions from the source image to the destination.
	 * @details Fundamental operation used to copy data between images or buffers within GPU memory.
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target image regions
	 * @param count region array size
	 */
	static void copy(ID<Image> source, ID<Image> destination, const CopyImageRegion* regions, uint32 count);
	/**
	 * @brief Copies data regions from the source buffer to the destination image.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source buffer
	 * @param destination destination image
	 * @param[in] regions target copy regions
	 * @param count region array size
	 */
	static void copy(ID<Buffer> source, ID<Image> destination, const CopyBufferRegion* regions, uint32 count);
	/**
	 * @brief Copies data regions from the source image to the destination buffer.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source image
	 * @param destination destination buffer
	 * @param[in] regions target copy regions
	 * @param count region array size
	 */
	static void copy(ID<Image> source, ID<Buffer> destination, const CopyBufferRegion* regions, uint32 count);
	
	/*******************************************************************************************************************
	 * @brief Copies data regions from the source image to the destination.
	 * @details See the @ref Image::copy().
	 * 
	 * @tparam N region array size
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target image region array
	 */
	template<psize N>
	static void copy(ID<Image> source, ID<Image> destination, const array<CopyImageRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	/**
	 * @brief Copies data regions from the source buffer to the destination image.
	 * @details See the @ref Image::copy().
	 * 
	 * @tparam N region array size
	 * @param source source buffer
	 * @param destination destination image
	 * @param[in] regions target copy region array
	 */
	template<psize N>
	static void copy(ID<Buffer> source, ID<Image> destination, const array<CopyBufferRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	/**
	 * @brief Copies data regions from the source image to the destination buffer.
	 * @details See the @ref Image::copy().
	 * 
	 * @tparam N region array size
	 * @param source source image
	 * @param destination destination buffer
	 * @param[in] regions target copy region array
	 */
	template<psize N>
	static void copy(ID<Image> source, ID<Buffer> destination, const array<CopyBufferRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }

	/*******************************************************************************************************************
	 * @brief Copies data regions from the source image to the destination.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target image region vector
	 */
	static void copy(ID<Image> source, ID<Image> destination, const vector<CopyImageRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Copies data regions from the source buffer to the destination image.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source buffer
	 * @param destination destination image
	 * @param[in] regions target copy region vector
	 */
	static void copy(ID<Buffer> source, ID<Image> destination, const vector<CopyBufferRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Copies data regions from the source image to the destination buffer.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source image
	 * @param destination destination buffer
	 * @param[in] regions target copy region vector
	 */
	static void copy(ID<Image> source, ID<Buffer> destination, const vector<CopyBufferRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }

	/*******************************************************************************************************************
	 * @brief Copies data region from the source image to the destination.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] region target image region
	 */
	static void copy(ID<Image> source, ID<Image> destination, const CopyImageRegion& region)
	{ copy(source, destination, &region, 1); }
	/**
	 * @brief Copies data region from the source buffer to the destination image.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source buffer
	 * @param destination destination image
	 * @param[in] region target copy region
	 */
	static void copy(ID<Buffer> source, ID<Image> destination, const CopyBufferRegion& region)
	{ copy(source, destination, &region, 1); }
	/**
	 * @brief Copies data region from the source image to the destination buffer.
	 * @details See the @ref Image::copy().
	 * 
	 * @param source source image
	 * @param destination destination buffer
	 * @param[in] region target copy region
	 */
	static void copy(ID<Image> source, ID<Buffer> destination, const CopyBufferRegion& region)
	{ copy(source, destination, &region, 1); }
	
	/*******************************************************************************************************************
	 * @brief Copies all data from the source image to the destination.
	 * @details See the @ref Image::copy().
	 * @note Source and destination image sizes should be the same.
	 * 
	 * @param source source image
	 * @param destination destination image
	 */
	static void copy(ID<Image> source, ID<Image> destination)
	{ CopyImageRegion region; copy(source, destination, &region, 1); }
	/**
	 * @brief Copies all data from the source buffer to the destination image.
	 * @details See the @ref Image::copy().
	 * @note Source buffer and destination image binary sizes should be the same.
	 * 
	 * @param source source buffer
	 * @param destination destination image
	 */
	static void copy(ID<Buffer> source, ID<Image> destination)
	{ CopyBufferRegion region; copy(source, destination, &region, 1); }
	/**
	 * @brief Copies all data from the source image to the destination buffer.
	 * @details See the @ref Image::copy().
	 * @note Source image and destination buffer binary sizes should be the same.
	 * 
	 * @param source source image
	 * @param destination destination image
	 */
	static void copy(ID<Image> source, ID<Buffer> destination)
	{ CopyBufferRegion region; copy(source, destination, &region, 1); }

	/*******************************************************************************************************************
	 * @brief Blits regions from the source image to the destination.
	 * 
	 * @details
	 * Operation that performs a bit-block transfer, which is essentially copying from one image to another with 
	 * the option to perform scaling, filtering and format conversion during the copy. This command is particularly 
	 * useful for operations where you need to resize images, or when you need to copy and potentially modify the 
	 * image data between different formats or resolutions.
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target blit regions
	 * @param count region array size
	 * @param filter blitting operation filter
	 */
	static void blit(ID<Image> source, ID<Image> destination, const BlitRegion* regions,
		uint32 count, Sampler::Filter filter = Sampler::Filter::Nearest);

	/**
	 * @brief Blits regions from the source image to the destination.
	 * @details See the @ref Image::blit().
	 * 
	 * @tparam N region array size
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target blit region array
	 * @param filter blitting operation filter
	 */
	template<psize N>
	static void blit(ID<Image> source, ID<Image> destination, const array<BlitRegion, N>& regions,
		Sampler::Filter filter = Sampler::Filter::Nearest)
	{ blit(source, destination, regions.data(), (uint32)N, filter); }
	/**
	 * @brief Blits regions from the source image to the destination.
	 * @details See the @ref Image::blit().
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] regions target blit region vector
	 * @param filter blitting operation filter
	 */
	static void blit(ID<Image> source, ID<Image> destination, const vector<BlitRegion>& regions,
		Sampler::Filter filter = Sampler::Filter::Nearest)
	{ blit(source, destination, regions.data(), (uint32)regions.size(), filter); }
	/**
	 * @brief Blits region from the source image to the destination.
	 * @details See the @ref Image::blit().
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param[in] region target blit region
	 * @param filter blitting operation filter
	 */
	static void blit(ID<Image> source, ID<Image> destination,
		const BlitRegion& region, Sampler::Filter filter = Sampler::Filter::Nearest)
	{ blit(source, destination, &region, 1, filter); }
	/**
	 * @brief Blits an entire source image to the destination.
	 * @details See the @ref Image::blit().
	 * @note Source and destination image sizes should be the same.
	 * 
	 * @param source source image
	 * @param destination destination image
	 * @param filter blitting operation filter
	 */
	static void blit(ID<Image> source, ID<Image> destination, Sampler::Filter filter = Sampler::Filter::Nearest)
	{ BlitRegion region; blit(source, destination, &region, 1, filter); }

	// TODO: add support of self copying and blitting if regions not overlapping.
};

/**
 * @brief Image bind type count.
 */
constexpr uint8 imageBindCount = 10;

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Image::Bind)

/***********************************************************************************************************************
 * @brief View of the graphics image.
 * 
 * @details 
 * Describes how to access an image and which part of the image to access. It acts as an interface between 
 * the image data and shader programs or fixed-function stages of the pipeline, allowing them to interpret 
 * the image data in a specific way. Image views do not change the underlying image data, instead, 
 * they define a view into the image, specifying aspects like the format, dimensionality, 
 * and which mip levels and array layers are accessible.
 */
class ImageView final : public Resource
{
	ID<Image> image = {};
	uint32 baseLayer = 0;
	uint32 layerCount = 0;
	uint8 baseMip = 0;
	uint8 mipCount = 0;
	Image::Type type = {};
	Image::Format format = {};
	bool _default = false;

	ImageView(bool isDefault, ID<Image> image, Image::Type type, Image::Format format,
		uint32 baseLayer, uint32 layerCount, uint8 baseMip, uint8 mipCount);
	bool destroy() final;

	friend class ImageViewExt;
	friend class LinearPool<ImageView>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty image view data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access image views.
	 */
	ImageView() = default;

	/**
	 * @brief Returns parent image.
	 * @details See the @ref Image.
	 */
	ID<Image> getImage() const noexcept { return image; }
	/**
	 * @brief Returns image base array layer.
	 * @details See the @ref Image.
	 */
	uint32 getBaseLayer() const noexcept { return baseLayer; }
	/**
	 * @brief Returns image array layer count.
	 * @details See the @ref Image.
	 */
	uint32 getLayerCount() const noexcept { return layerCount; }
	/**
	 * @brief Returns image base mipmap level.
	 * @details See the @ref Image.
	 */
	uint8 getBaseMip() const noexcept { return baseMip; }
	/**
	 * @brief Returns image mipmap level count.
	 * @details See the @ref Image.
	 */
	uint8 getMipCount() const noexcept { return mipCount; }
	/**
	 * @brief Returns image dimensionality type.
	 * @details See the @ref Image.
	 */
	Image::Type getType() const noexcept { return type; }
	/**
	 * @brief Returns image data format.
	 * @details See the @ref Image.
	 */
	Image::Format getFormat() const noexcept { return format; }
	/**
	 * @brief Is image view default.
	 * @details See the @ref Image.
	 */
	bool isDefault() const noexcept { return _default; }

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets image view debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif
};

/***********************************************************************************************************************
 * @brief Is the image data format a color.
 * @param formatType target image format
 */
static constexpr bool isFormatColor(Image::Format formatType)
{
	return (uint8)Image::Format::Undefined < (uint8)formatType &&
		(uint8)formatType < (uint8)Image::Format::UnormD16;
}
/**
 * @brief Is the image data format a depth only.
 * @param formatType target image format
 */
static constexpr bool isFormatDepthOnly(Image::Format formatType)
{
	return (uint8)Image::Format::UnormD16 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SfloatD32;
}
/**
 * @brief Is the image data format a stencil only.
 * @param formatType target image format
 */
static constexpr bool isFormatStencilOnly(Image::Format formatType)
{
	return false; // TODO:
}
/**
 * @brief Is the image data format a combined depth/stencil.
 * @param formatType target image format
 */
static constexpr bool isFormatDepthStencil(Image::Format formatType)
{
	return (uint8)Image::Format::UnormD24UintS8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SfloatD32Uint8S;
}
/**
 * @brief Is the image data format a depth or stencil.
 * @param formatType target image format
 */
static constexpr bool isFormatDepthOrStencil(Image::Format formatType)
{
	return (uint8)Image::Format::UnormD16 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SfloatD32Uint8S;
}
/**
 * @brief Is the image data format an unsigned integer.
 * @param formatType target image format
 */
static constexpr bool isFormatUint(Image::Format formatType)
{
	return (uint8)Image::Format::UintR8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::UintA2B10G10R10;
}
/**
 * @brief Is the image data format a signed integer.
 * @param formatType target image format
 */
static constexpr bool isFormatInt(Image::Format formatType)
{
	return (uint8)Image::Format::SintR8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SintR32G32B32A32;
}
/**
 * @brief Is the image data format a normalized unsigned integer.
 * @param formatType target image format
 */
static constexpr bool isFormatUnorm(Image::Format formatType)
{
	return (uint8)Image::Format::UnormR8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::UnormA2B10G10R10;
}
/**
 * @brief Is the image data format a normalized signed integer.
 * @param formatType target image format
 */
static constexpr bool isFormatSnorm(Image::Format formatType)
{
	return (uint8)Image::Format::SnormR8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SnormR16G16B16A16;
}
/**
 * @brief Is the image data format a normalized signed or unsigned integer.
 * @param formatType target image format
 */
static constexpr bool isFormatNorm(Image::Format formatType)
{
	return (uint8)Image::Format::UnormR8 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::SnormR16G16B16A16;
}
/**
 * @brief Is the image data format a floating point.
 * @param formatType target image format
 */
static constexpr bool isFormatFloat(Image::Format formatType)
{
	return (uint8)Image::Format::SfloatR16 <= (uint8)formatType &&
		(uint8)formatType <= (uint8)Image::Format::UfloatB10G11R11;
}

/***********************************************************************************************************************
 * @brief Returns image data format binary size in bytes.
 * @param imageFormat target image data format
 * @warning Same size is not guaranteed on the GPU!
 */
static constexpr psize toBinarySize(Image::Format imageFormat) noexcept
{
	switch (imageFormat)
	{
	case Image::Format::UintR8: return 1;
	case Image::Format::UintR8G8: return 2;
	case Image::Format::UintR8G8B8A8: return 4;
	case Image::Format::UintR16: return 2;
	case Image::Format::UintR16G16: return 4;
	case Image::Format::UintR16G16B16A16: return 8;
	case Image::Format::UintR32: return 4;
	case Image::Format::UintR32G32: return 8;
	case Image::Format::UintR32G32B32A32: return 16;
	case Image::Format::UintA2R10G10B10: return 4;
	case Image::Format::UintA2B10G10R10: return 4;

	case Image::Format::SintR8: return 1;
	case Image::Format::SintR8G8: return 2;
	case Image::Format::SintR8G8B8A8: return 4;
	case Image::Format::SintR16: return 2;
	case Image::Format::SintR16G16: return 4;
	case Image::Format::SintR16G16B16A16: return 8;
	case Image::Format::SintR32: return 4;
	case Image::Format::SintR32G32: return 8;
	case Image::Format::SintR32G32B32A32: return 16;

	case Image::Format::UnormR8: return 1;
	case Image::Format::UnormR8G8: return 2;
	case Image::Format::UnormR8G8B8A8: return 4;
	case Image::Format::UnormB8G8R8A8: return 4;
	case Image::Format::UnormR16: return 2;
	case Image::Format::UnormR16G16: return 4;
	case Image::Format::UnormR16G16B16A16: return 8;
	case Image::Format::UnormR5G6B5: return 2;
	case Image::Format::UnormA1R5G5B5: return 2;
	case Image::Format::UnormR5G5B5A1: return 2;
	case Image::Format::UnormB5G5R5A1: return 2;
	case Image::Format::UnormR4G4B4A4: return 2;
	case Image::Format::UnormB4G4R4A4: return 2;
	case Image::Format::UnormA2R10G10B10: return 4;
	case Image::Format::UnormA2B10G10R10: return 4;

	case Image::Format::SnormR8: return 1;
	case Image::Format::SnormR8G8: return 2;
	case Image::Format::SnormR8G8B8A8: return 4;
	case Image::Format::SnormR16: return 2;
	case Image::Format::SnormR16G16: return 4;
	case Image::Format::SnormR16G16B16A16: return 8;

	case Image::Format::SfloatR16: return 2;
	case Image::Format::SfloatR16G16: return 4;
	case Image::Format::SfloatR16G16B16A16: return 8;
	case Image::Format::SfloatR32: return 4;
	case Image::Format::SfloatR32G32: return 8;
	case Image::Format::SfloatR32G32B32A32: return 16;

	case Image::Format::UfloatB10G11R11: return 4;
	case Image::Format::UfloatE5B9G9R9: return 4;

	case Image::Format::SrgbR8G8B8A8: return 4;
	case Image::Format::SrgbB8G8R8A8: return 4;
	
	case Image::Format::UnormD16: return 2;
	case Image::Format::SfloatD32: return 4;
	case Image::Format::UnormD24UintS8: return 4;
	case Image::Format::SfloatD32Uint8S: return 5;
	
	default: return 0;
	}
}

/***********************************************************************************************************************
 * @brief Returns image dimensionality type from the uniform type.
 * @param uniformType target uniform type
 * @throw GardenError on unsupported uniform type.
 */
static Image::Type toImageType(GslUniformType uniformType)
{
	switch (uniformType)
	{
	case GslUniformType::Sampler1D:
	case GslUniformType::Isampler1D:
	case GslUniformType::Usampler1D:
	case GslUniformType::Sampler1DShadow:
	case GslUniformType::Image1D:
	case GslUniformType::Iimage1D:
	case GslUniformType::Uimage1D:
		return Image::Type::Texture1D;
	case GslUniformType::Sampler2D:
	case GslUniformType::Isampler2D:
	case GslUniformType::Usampler2D:
	case GslUniformType::Sampler2DShadow:
	case GslUniformType::Image2D:
	case GslUniformType::Iimage2D:
	case GslUniformType::Uimage2D:
		return Image::Type::Texture2D;
	case GslUniformType::Sampler3D:
	case GslUniformType::Isampler3D:
	case GslUniformType::Usampler3D:
	case GslUniformType::Image3D:
	case GslUniformType::Iimage3D:
	case GslUniformType::Uimage3D:
		return Image::Type::Texture3D;
	case GslUniformType::Sampler1DArray:
	case GslUniformType::Isampler1DArray:
	case GslUniformType::Usampler1DArray:
	case GslUniformType::Sampler1DArrayShadow:
	case GslUniformType::Image1DArray:
	case GslUniformType::Iimage1DArray:
	case GslUniformType::Uimage1DArray:
		return Image::Type::Texture1DArray;
	case GslUniformType::Sampler2DArray:
	case GslUniformType::Isampler2DArray:
	case GslUniformType::Usampler2DArray:
	case GslUniformType::Sampler2DArrayShadow:
	case GslUniformType::Image2DArray:
	case GslUniformType::Iimage2DArray:
	case GslUniformType::Uimage2DArray:
		return Image::Type::Texture2DArray;
	case GslUniformType::SamplerCube:
	case GslUniformType::IsamplerCube:
	case GslUniformType::UsamplerCube:
	case GslUniformType::SamplerCubeShadow:
	case GslUniformType::ImageCube:
	case GslUniformType::IimageCube:
	case GslUniformType::UimageCube:
		return Image::Type::Cubemap;
	default: throw GardenError("Unsupported image type. ("
		"uniformType: " + to_string((uint8)uniformType) + ")");
	}
}

/***********************************************************************************************************************
 * @brief Returns image bind name string.
 * @param imageBind target image bind type
 */
static string_view toString(Image::Bind imageBind) noexcept
{
	if (hasOneFlag(imageBind, Image::Bind::TransferSrc)) return "TransferSrc";
	if (hasOneFlag(imageBind, Image::Bind::TransferDst)) return "TransferDst";
	if (hasOneFlag(imageBind, Image::Bind::Sampled)) return "Sampled";
	if (hasOneFlag(imageBind, Image::Bind::Storage)) return "Storage";
	if (hasOneFlag(imageBind, Image::Bind::ColorAttachment)) return "ColorAttachment";
	if (hasOneFlag(imageBind, Image::Bind::DepthStencilAttachment)) return "DepthStencilAttachment";
	if (hasOneFlag(imageBind, Image::Bind::InputAttachment)) return "InputAttachment";
	if (hasOneFlag(imageBind, Image::Bind::Fullscreen)) return "Fullscreen";
	return "None";
}
/**
 * @brief Returns image bind name string list.
 * @param imageBind target image bind type
 */
static string toStringList(Image::Bind imageBind) noexcept
{
	string list;
	if (hasAnyFlag(imageBind, Image::Bind::None)) list += "None | ";
	if (hasAnyFlag(imageBind, Image::Bind::TransferSrc)) list += "TransferSrc | ";
	if (hasAnyFlag(imageBind, Image::Bind::TransferDst)) list += "TransferDst | ";
	if (hasAnyFlag(imageBind, Image::Bind::Sampled)) list += "Sampled | ";
	if (hasAnyFlag(imageBind, Image::Bind::Storage)) list += "Storage | ";
	if (hasAnyFlag(imageBind, Image::Bind::ColorAttachment)) list += "ColorAttachment | ";
	if (hasAnyFlag(imageBind, Image::Bind::DepthStencilAttachment)) list += "DepthStencilAttachment | ";
	if (hasAnyFlag(imageBind, Image::Bind::InputAttachment)) list += "InputAttachment | ";
	if (hasAnyFlag(imageBind, Image::Bind::Fullscreen)) list += "Fullscreen | ";
	if (list.length() >= 3) list.resize(list.length() - 3);
	return list;
}

/***********************************************************************************************************************
 * @brief Image dimensionality type name strings.
 */
constexpr string_view imageTypeNames[(psize)Image::Type::Count] =
{
	"Texture1D", "Texture2D", "Texture3D", "Texture1DArray", "Texture2DArray", "Cubemap"
};
/**
 * @brief Image data format name strings.
 */
constexpr string_view imageFormatNames[(psize)Image::Format::Count] =
{
	"Undefined",
	
	"UintR8", "UintR8G8", "UintR8G8B8A8", "UintR16", "UintR16G16", "UintR16G16B16A16", 
	"UintR32", "UintR32G32", "UintR32G32B32A32", "UintA2R10G10B10", "UintA2B10G10R10", 

	"SintR8", "SintR8G8", "SintR8G8B8A8", "SintR16", "SintR16G16", 
	"SintR16G16B16A16", "SintR32", "SintR32G32", "SintR32G32B32A32", 

	"UnormR8", "UnormR8G8", "UnormR8G8B8A8", "UnormB8G8R8A8", "UnormR16", "UnormR16G16", 
	"UnormR16G16B16A16", "UnormR5G6B5", "UnormA1R5G5B5", "UnormR5G5B5A1", "UnormB5G5R5A1", 
	"UnormR4G4B4A4", "UnormB4G4R4A4", "UnormA2R10G10B10", "UnormA2B10G10R10", 

	"SnormR8", "SnormR8G8", "SnormR8G8B8A8", "SnormR16", "SnormR16G16", "SnormR16G16B16A16", 

	"SfloatR16", "SfloatR16G16", "SfloatR16G16B16A16", "SfloatR32", "SfloatR32G32", "SfloatR32G32B32A32", 

	"UfloatB10G11R11", "UfloatE5B9G9R9", 

	"SrgbR8G8B8A8", "SrgbB8G8R8A8", 

	"UnormD16", "SfloatD32", "UnormD24UintS8", "SfloatD32Uint8S"
};

/**
 * @brief Returns image dimensionality type name string.
 * @param imageType target image dimensionality type
 */
static string_view toString(Image::Type imageType) noexcept
{
	GARDEN_ASSERT((uint8)imageType < (uint8)Image::Type::Count);
	return imageTypeNames[(psize)imageType];
}
/**
 * @brief Returns image data format name string.
 * @param imageFormat target image data format
 */
static string_view toString(Image::Format imageFormat) noexcept
{
	GARDEN_ASSERT((uint8)imageFormat < (uint8)Image::Format::Count);
	return imageFormatNames[(psize)imageFormat];
}

/***********************************************************************************************************************
 * @brief Graphics image resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class ImageExt final
{
public:
	/**
	 * @brief Returns image dimensionality type.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static Image::Type& getType(Image& image) noexcept { return image.type; }
	/**
	 * @brief Returns image data format.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static Image::Format& getFormat(Image& image) noexcept { return image.format; }
	/**
	 * @brief Returns image bind type.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static Image::Bind& getBind(Image& image) noexcept { return image.bind; }
	/**
	 * @brief Is this image part of the swapchain.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static bool& isSwapchain(Image& image) noexcept { return image.swapchain; }
	/**
	 * @brief Returns image layer nad mipmap layouts.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static vector<uint32>& getLayouts(Image& image) noexcept { return image.layouts; }
	/**
	 * @brief Returns image size in texels.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static u32x4& getSize(Image& image) noexcept { return image.size; }
	/**
	 * @brief Returns default image view.
	 * @warning In most cases you should use @ref Image functions.
	 * @param[in] image target image instance
	 */
	static ID<ImageView> getDefaultView(Image& image) noexcept { return image.defaultView; }

	/**
	 * @brief Creates a new image data.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param type image dimensionality type
	 * @param format image data format
	 * @param bind image bind usage
	 * @param strategy image allocation strategy
	 * @param size image size in texels and mip count
	 * @param mipCount image mipmap level count
	 * @param version image instance version
	 */
	static Image create(Image::Type type, Image::Format format, Image::Bind bind,
		Image::Strategy strategy, u32x4 size, uint64 version)
	{
		return Image(type, format, bind, strategy, size, version);
	}
	/**
	 * @brief Moves internal image objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source image instance
	 * @param[in,out] destination destination image instance
	 */
	static void moveInternalObjects(Image& source, Image& destination) noexcept
	{
		MemoryExt::getAllocation(destination) = MemoryExt::getAllocation(source);
		MemoryExt::getBinarySize(destination) = MemoryExt::getBinarySize(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		ImageExt::getType(destination) = ImageExt::getType(source);
		ImageExt::getFormat(destination) = ImageExt::getFormat(source);
		ImageExt::getSize(destination) = ImageExt::getSize(source);
		ImageExt::getLayouts(destination) = std::move(ImageExt::getLayouts(source));
		ResourceExt::getInstance(source) = nullptr;
	}
	/**
	 * @brief Destroys image instance.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * @param[in,out] image target image instance
	 */
	static void destroy(Image& image) { image.destroy(); }
};

/***********************************************************************************************************************
 * @brief Graphics image view resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class ImageViewExt final
{
public:
	/**
	 * @brief Returns parent image.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static ID<Image>& getImage(ImageView& imageView) noexcept { return imageView.image; }
	/**
	 * @brief Returns image base array layer.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static uint32& getBaseLayer(ImageView& imageView) noexcept { return imageView.baseLayer; }
	/**
	 * @brief Returns image array layer count.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static uint32& getLayerCount(ImageView& imageView) noexcept { return imageView.layerCount; }
	/**
	 * @brief Returns image base mipmap level.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static uint8& getBaseMip(ImageView& imageView) noexcept { return imageView.baseMip; }
	/**
	 * @brief Returns image mipmap level count.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static uint8& getMipCount(ImageView& imageView) noexcept { return imageView.mipCount; }
	/**
	 * @brief Returns image dimensionality type.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static Image::Type& getType(ImageView& imageView) noexcept { return imageView.type; }
	/**
	 * @brief Returns image data format.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static Image::Format& getFormat(ImageView& imageView) noexcept { return imageView.format; }
	/**
	 * @brief Is image view default.
	 * @warning In most cases you should use @ref ImageView functions.
	 * @param[in] imageView target image view instance
	 */
	static bool& isDefault(ImageView& imageView) noexcept { return imageView._default; }
};

/**
 * @brief Converts floating point value to the B10G11R11 value.
 *
 * @param value target floating point value
 * @param bits packed mantissa bit count
 * @param mask packed mantissa mask
 */
static uint32 encodeB10G11R11(float value, uint32 bits, uint32 mask) noexcept
{
	auto valueBits = *(const uint32*)&value;
	auto exponent = (int32)((valueBits >> 23u) & 0xFFu) - 127 + 15; 
	if (exponent <= 0) return 0; // 5-bit exponent bias 15
	auto mantissa = (valueBits >> 12u) & mask;
	return (exponent << bits) | mantissa;
}
/**
 * @brief Converts 3D floating point vector to the B10G11R11 value.
 * @param rgb target 3D floating point vector
 */
static uint32 encodeB10G11R11(f32x4 rgb) noexcept
{
	rgb = clamp(rgb, f32x4::zero, f32x4(65504.0f));
	auto r = encodeB10G11R11(rgb.getX(), 6, 0b111111);
	auto g = encodeB10G11R11(rgb.getY(), 6, 0b111111);
	auto b = encodeB10G11R11(rgb.getZ(), 5, 0b11111);
	return (b << 22u) | (g << 11u) | r;
}

} // namespace garden::graphics

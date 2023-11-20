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

#pragma once
#include "garden/graphics/gsl.hpp"
#include "garden/graphics/buffer.hpp"

// Image Format Identification
// Sfloat - signed float
// Ufloat - unsigned float
// Uint - unsigned integer
// Sint - signed integer
// Unorm - uint float [0.0, 1.0] (255 -> 1.0)
// Snorm - int float [-1.0, 1.0] (0 -> -1.0)
// Uscaled - uint as float (128 -> 128.0)
// Sscaled - int as float (-32 -> -32.0)
// Srgb - sRGB color space

namespace garden::graphics
{

using namespace math;
using namespace ecsm;

class Image;
class ImageExt;
class ImageView;

//--------------------------------------------------------------------------------------------------
class Image final : public Memory
{
public:
	enum class Type : uint8
	{
		Texture1D, Texture2D, Texture3D, Texture1DArray, Texture2DArray, Cubemap, Count
	};
	enum class Format : uint8
	{
		Undefined, UintR8, UintR16, UintR32,
		UnormR8, UnormR8G8, UnormR8G8B8A8, UnormB8G8R8A8, SrgbR8G8B8A8, SrgbB8G8R8A8,
		SfloatR16G16, SfloatR32G32, SfloatR16G16B16A16, SfloatR32G32B32A32,
		UnormA2R10G10B10, UfloatB10G11R11,
		UnormD16, SfloatD32, UnormD24UintS8, SfloatD32Uint8S, Count
	};
	enum class Bind : uint8
	{
		None = 0x00, TransferSrc = 0x01, TransferDst = 0x02, Sampled = 0x04, Storage = 0x08,
		ColorAttachment = 0x10, DepthStencilAttachment = 0x20, InputAttachment = 0x40,
		Fullscreen = 0x80,
	};

	struct ClearRegion final
	{
		uint32 baseMip = 0;
		uint32 mipCount = 0;
		uint32 baseLayer = 0;
		uint32 layerCount = 0;
	};
	struct CopyImageRegion final
	{
		int3 srcOffset = int3(0);
		int3 dstOffset = int3(0);
		int3 extent = int3(0);
		uint32 srcBaseLayer = 0;
		uint32 dstBaseLayer = 0;
		uint32 layerCount = 0;
		uint32 srcMipLevel = 0;
		uint32 dstMipLevel = 0;
	};
	struct CopyBufferRegion final
	{
		uint64 bufferOffset = 0;
		uint32 bufferRowLength = 0;
		uint32 bufferImageHeight = 0;
		int3 imageOffset = int3(0);
		int3 imageExtent = int3(0);
		uint32 imageBaseLayer = 0;
		uint32 imageLayerCount = 0;
		uint32 imageMipLevel = 0;
	};
	struct BlitRegion final
	{
		int3 srcOffset = int3(0);
		int3 srcExtent = int3(0);
		int3 dstOffset = int3(0);
		int3 dstExtent = int3(0);
		uint32 srcBaseLayer = 0;
		uint32 dstBaseLayer = 0;
		uint32 layerCount = 0;
		uint32 srcMipLevel = 0;
		uint32 dstMipLevel = 0;
	};

	using Layers = vector<const void*>;
	using Mips = vector<Layers>;
//--------------------------------------------------------------------------------------------------
private:
	Type type = {};
	Format format = {};
	Bind bind = {};
	ID<ImageView> defaultView = {};
	vector<uint32> layouts;
	int3 size = int3(0);
	uint32 layerCount = 0;
	uint8 mipCount = 0;
	bool swapchain = false;
	
	Image() = default;
	Image(Type type, Format format, Bind bind, const int3& size,
		uint8 mipCount, uint32 layerCount, uint64 version);
	Image(Bind bind, uint64 version) :
		Memory(0, Usage::GpuOnly, version) { this->bind = bind; }
	Image(void* instance, Format format, Bind bind, int2 size, uint64 version);
	bool destroy() final;

	friend class Vulkan;
	friend class Pipeline;
	friend class ImageExt;
	friend class ImageView;
	friend class Swapchain;
	friend class CommandBuffer;
	friend class LinearPool<Image>;
public:
	const int3& getSize() const noexcept { return size; }
	Type getType() const noexcept { return type; }
	Format getFormat() const noexcept { return format; }
	Bind getBind() const noexcept { return bind; }
	uint8 getMipCount() const noexcept { return mipCount; }
	uint32 getLayerCount() const noexcept { return layerCount; }
	bool isSwapchain() const noexcept { return swapchain; }
	ID<ImageView> getDefaultView();	

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	void generateMips(SamplerFilter filter = SamplerFilter::Linear);

	void clear(const float4& color, const ClearRegion* regions, uint32 count);
	void clear(const int4& color, const ClearRegion* regions, uint32 count);
	// TODO: clear(uint);
	void clear(float depth, uint32 stencil, const ClearRegion* regions, uint32 count);

	template<psize N>
	void clear(const float4& color, const array<ClearRegion, N>& regions)
	{ clear(color, regions.data(), (uint32)N); }
	template<psize N>
	void clear(const int4& color, const array<ClearRegion, N>& regions)
	{ clear(color, regions.data(), (uint32)N); }
	template<psize N>
	void clear(float depth, uint32 stencil, const array<ClearRegion, N>& regions)
	{ clear(depth, stencil, regions.data(), (uint32)N); }

	void clear(const float4& color, const vector<ClearRegion>& regions)
	{ clear(color, regions.data(), (uint32)regions.size()); }
	void clear(const int4& color, const vector<ClearRegion>& regions)
	{ clear(color, regions.data(), (uint32)regions.size()); }
	void clear(float depth, uint32 stencil, const vector<ClearRegion>& regions)
	{ clear(depth, stencil, regions.data(), (uint32)regions.size()); }

	void clear(const float4& color, const ClearRegion& region)
	{ clear(color, &region, 1); }
	void clear(const int4& color, const ClearRegion& region)
	{ clear(color, &region, 1); }
	void clear(float depth, uint32 stencil, const ClearRegion& region)
	{ clear(depth, stencil, &region, 1); }

	void clear(const float4& color)
	{ ClearRegion region; clear(color, &region, 1); }
	void clear(const int4& color)
	{ ClearRegion region; clear(color, &region, 1); }
	void clear(float depth, uint32 stencil)
	{ ClearRegion region; clear(depth, stencil, &region, 1); }

	// TODO: add support of self copying and bliting if regions not overlapping.
	static void copy(ID<Image> source, ID<Image> destination,
		const CopyImageRegion* regions, uint32 count);
	static void copy(ID<Buffer> source, ID<Image> destination,
		const CopyBufferRegion* regions, uint32 count);
	static void copy(ID<Image> source, ID<Buffer> destination,
		const CopyBufferRegion* regions, uint32 count);
	
	template<psize N>
	static void copy(ID<Image> source, ID<Image> destination,
		const array<CopyImageRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	template<psize N>
	static void copy(ID<Buffer> source, ID<Image> destination,
		const array<CopyBufferRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	template<psize N>
	static void copy(ID<Image> source, ID<Buffer> destination,
		const array<CopyBufferRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }

	static void copy(ID<Image> source, ID<Image> destination,
		const vector<CopyImageRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	static void copy(ID<Buffer> source, ID<Image> destination,
		const vector<CopyBufferRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	static void copy(ID<Image> source, ID<Buffer> destination,
		const vector<CopyBufferRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }

	static void copy(ID<Image> source, ID<Image> destination,
		const CopyImageRegion& region)
	{ copy(source, destination, &region, 1); }
	static void copy(ID<Buffer> source, ID<Image> destination,
		const CopyBufferRegion& region)
	{ copy(source, destination, &region, 1); }
	static void copy(ID<Image> source, ID<Buffer> destination,
		const CopyBufferRegion& region)
	{ copy(source, destination, &region, 1); }
	
	static void copy(ID<Image> source, ID<Image> destination)
	{ CopyImageRegion region; copy(source, destination, &region, 1); }
	static void copy(ID<Buffer> source, ID<Image> destination)
	{ CopyBufferRegion region; copy(source, destination, &region, 1); }
	static void copy(ID<Image> source, ID<Buffer> destination)
	{ CopyBufferRegion region; copy(source, destination, &region, 1); }

	static void blit(ID<Image> source, ID<Image> destination,
		const BlitRegion* regions, uint32 count,
		SamplerFilter filter = SamplerFilter::Nearest);

	template<psize N>
	static void blit(ID<Image> source, ID<Image> destination,
		const array<BlitRegion, N>& regions,
		SamplerFilter filter = SamplerFilter::Nearest)
	{ blit(source, destination, regions.data(), (uint32)N, filter); }
	static void blit(ID<Image> source, ID<Image> destination,
		const vector<BlitRegion>& regions,
		SamplerFilter filter = SamplerFilter::Nearest)
	{ blit(source, destination, regions.data(), (uint32)regions.size(), filter); }
	static void blit(ID<Image> source, ID<Image> destination,
		const BlitRegion& region, SamplerFilter filter = SamplerFilter::Nearest)
	{ blit(source, destination, &region, 1, filter); }
	static void blit(ID<Image> source, ID<Image> destination,
		SamplerFilter filter = SamplerFilter::Nearest)
	{ BlitRegion region; blit(source, destination, &region, 1, filter); }
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Image::Bind)

//--------------------------------------------------------------------------------------------------
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

	ImageView() = default;
	ImageView(bool isDefault, ID<Image> image, Image::Type type, Image::Format format,
		uint8 baseMip, uint8 mipCount, uint32 baseLayer, uint32 layerCount);
	bool destroy() final;

	friend class Pipeline;
	friend class Framebuffer;
	friend class DescriptorSet;
	friend class CommandBuffer;
	friend class LinearPool<ImageView>;
public:
	ID<Image> getImage() const noexcept { return image; }
	uint32 getBaseLayer() const noexcept { return baseLayer; }
	uint32 getLayerCount() const noexcept { return layerCount; }
	uint8 getBaseMip() const noexcept { return baseMip; }
	uint8 getMipCount() const noexcept { return mipCount; }
	Image::Type getType() const noexcept { return type; }
	Image::Format getFormat() const noexcept { return format; }
	bool isDefault() const noexcept { return _default; }

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif
};

//--------------------------------------------------------------------------------------------------
static psize toBinarySize(Image::Format imageFormat)
{
	// Note: not guaranteed same size on hardware.
	switch (imageFormat)
	{
	case Image::Format::UintR8: return 1;
	case Image::Format::UintR16: return 2;
	case Image::Format::UintR32: return 4;
	case Image::Format::UnormR8: return 1;
	case Image::Format::UnormR8G8: return 2;
	case Image::Format::UnormR8G8B8A8:
	case Image::Format::UnormB8G8R8A8:
	case Image::Format::SrgbR8G8B8A8:
	case Image::Format::SrgbB8G8R8A8:
		return 4;
	case Image::Format::SfloatR16G16: return 4;
	case Image::Format::SfloatR32G32: return 8;
	case Image::Format::SfloatR16G16B16A16: return 8;
	case Image::Format::SfloatR32G32B32A32: return 16;
	case Image::Format::UnormA2R10G10B10: return 4;
	case Image::Format::UfloatB10G11R11: return 4;
	case Image::Format::UnormD16: return 2;
	case Image::Format::SfloatD32: return 4;
	case Image::Format::UnormD24UintS8: return 4;
	case Image::Format::SfloatD32Uint8S: return 5;
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
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
	default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
static string_view toString(Image::Bind imageBind)
{
	if (hasOneFlag(imageBind, Image::Bind::None)) return "None";
	if (hasOneFlag(imageBind, Image::Bind::TransferSrc)) return "TransferSrc";
	if (hasOneFlag(imageBind, Image::Bind::TransferDst)) return "TransferDst";
	if (hasOneFlag(imageBind, Image::Bind::Sampled)) return "Sampled";
	if (hasOneFlag(imageBind, Image::Bind::Storage)) return "Storage";
	if (hasOneFlag(imageBind, Image::Bind::ColorAttachment)) return "ColorAttachment";
	if (hasOneFlag(imageBind, Image::Bind::DepthStencilAttachment)) return "DepthStencilAttachment";
	if (hasOneFlag(imageBind, Image::Bind::InputAttachment)) return "InputAttachment";
	if (hasOneFlag(imageBind, Image::Bind::Fullscreen)) return "Fullscreen";
	throw runtime_error("Unknown image bind type. (" + to_string((int)imageBind) + ")");
}
static string toStringList(Image::Bind imageBind)
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
	list.resize(list.length() - 3);
	return list;
}

//--------------------------------------------------------------------------------------------------
static const string_view imageTypeNames[(psize)Image::Type::Count] =
{
	"Texture1D", "Texture2D", "Texture3D", "Texture1DArray", "Texture2DArray", "Cubemap"
};

static const string_view imageFormatNames[(psize)Image::Format::Count] =
{
	"Undefined", "UintR8", "UintR16", "UintR32",
	"UnormR8", "UnormR8G8", "UnormR8G8B8A8", "UnormB8G8R8A8", "SrgbR8G8B8A8", "SrgbB8G8R8A8",
	"SfloatR16G16", "SfloatR32G32", "SfloatR16G16B16A16", "SfloatR32G32B32A32",
	"UnormA2R10G10B10", "UfloatB10G11R11",
	"UnormD16", "SfloatD32", "UnormD24UintS8", "SfloatD32Uint8S",
};

static string_view toString(Image::Type imageType)
{
	GARDEN_ASSERT((uint8)imageType < (uint8)Image::Type::Count);
	return imageTypeNames[(psize)imageType];
}
static string_view toString(Image::Format imageFormat)
{
	GARDEN_ASSERT((uint8)imageFormat < (uint8)Image::Format::Count);
	return imageFormatNames[(psize)imageFormat];
}

//--------------------------------------------------------------------------------------------------
class ImageExt final
{
public:
	static Image::Type& getType(Image& image) noexcept { return image.type; }
	static Image::Format& getFormat(Image& image) noexcept { return image.format; }
	static Image::Bind& getBind(Image& image) noexcept { return image.bind; }
	static uint8& getMipCount(Image& image) noexcept { return image.mipCount; }
	static bool& isSwapchain(Image& image) noexcept { return image.swapchain; }
	static int3& getSize(Image& image) noexcept { return image.size; }
	static uint32& getLayerCount(Image& image) noexcept { return image.layerCount; }
	static ID<ImageView> getDefaultView(Image& image) noexcept { return image.defaultView; }
	static vector<uint32>& getLayouts(Image& image) noexcept { return image.layouts; }

	static Image create(Image::Type type, Image::Format format, Image::Bind bind,
		const int3& size, uint8 mipCount, uint32 layerCount, uint64 version)
	{
		return Image(type, format, bind, size, mipCount, layerCount, version);
	}
	static void moveInternalObjects(Image& source, Image& destination) noexcept
	{
		MemoryExt::getAllocation(destination) = MemoryExt::getAllocation(source);
		MemoryExt::getBinarySize(destination) = MemoryExt::getBinarySize(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		ImageExt::getType(destination) = ImageExt::getType(source);
		ImageExt::getFormat(destination) = ImageExt::getFormat(source);
		ImageExt::getMipCount(destination) = ImageExt::getMipCount(source);
		ImageExt::getSize(destination) = ImageExt::getSize(source);
		ImageExt::getLayerCount(destination) = ImageExt::getLayerCount(source);
		ImageExt::getLayouts(destination) = std::move(ImageExt::getLayouts(source));
		ResourceExt::getInstance(source) = nullptr;
	}
	static void destroy(Image& image) { image.destroy(); }
};

} // garden::graphics

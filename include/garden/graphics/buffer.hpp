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
 * @brief Common graphics buffer functions.
 */

#pragma once
#include "linear-pool.hpp"
#include "garden/graphics/common.hpp"
#include "garden/graphics/memory.hpp"

namespace garden::graphics
{

using namespace ecsm;
class BufferExt;

/**
 * @brief Common buffer data channels.
 */
enum class BufferChannel : uint8
{
	Positions, Normals, Tangents, Bitangents, TextureCoords, VertexColors, Count
};

/**
 * @brief Graphics rendering data storage.
 * 
 * @details
 * A fundamental resource representing a block of memory that can store arbitrary data. Buffers are 
 * versatile and can be used for a wide variety of purposes in graphics and compute operations. 
 * Unlike images, buffers provide a more generalized and linear storage solution. This makes them 
 * suitable for storing vertex data, indices, uniform data for shaders, compute shader inputs and 
 * outputs, and various other types of data that do not require the structured format of an image.
 * 
 * Vertex Buffers: Buffers can store vertex data, such as positions, normals, 
 *     texture coordinates, and other vertex attributes used in rendering.
 * Index Buffers: These are used to store indices for indexed drawing, 
 *     allowing reuse of vertex data for efficient rendering.
 * Uniform Buffers: Uniform buffers hold uniform data that is constant across a draw call, 
 *     such as transformation matrices or material properties, accessible to vertex and fragment shaders.
 * Storage Buffers: These buffers can be read from and written to by compute shaders, 
 *     allowing for more complex data manipulation and storage than uniform buffers.
 * Indirect Draw Buffers: Buffers can also store commands for indirect drawing, where draw commands are 
 *     sourced from buffer memory, allowing the GPU to determine draw parameters dynamically.
 * Staging Buffers: These are used for transferring data between the CPU and GPU. For example, 
 *     a staging buffer can be used to upload vertex or texture data to the GPU by first writing to a 
 *     buffer in a CPU-accessible memory, and then transferring the data to a device-local buffer or image.
 */
class Buffer final : public Memory
{
public:
	/**
	 * @brief Buffer usage types. (Affects driver optimizations)
	 * 
	 * @details
	 * Buffer usage flags are critical for ensuring that an buffer is compatible 
	 * with the operations the application intends to perform on it.
	 */
	enum class Usage : uint32
	{
		None          = 0x0000, /**< No buffer usage specified, zero mask. */
		TransferSrc   = 0x0001, /**< Buffer can be used as the source of a transfer command. */
		TransferDst   = 0x0002, /**< Buffer can be used as the destination of a transfer command. */
		Vertex        = 0x0004, /**< Buffer can be used by a graphics rendering commands. */
		Index         = 0x0008, /**< Buffer can be used by a graphics rendering commands. */
		Uniform       = 0x0010, /**< Buffer can be used in a descriptor set. (Faster but has small capacity) */
		Storage       = 0x0020, /**< Buffer can be used in a descriptor set. (Slower but has bigger capacity) */
		Indirect      = 0x0040, /**< Buffer can be used by an indirect rendering commands. */
		DeviceAddress = 0x0080, /**< Buffer device address can be used inside shaders. */
		StorageAS     = 0x0100, /**< Buffer can be used for a acceleration structure storage space. */
		BuildInputAS  = 0x0200, /**< Buffer can be used as a read only input for acceleration structure build. */
		SBT           = 0x0400  /**< Buffer can be used as a ray tracing shader binding table. */
	};

	static constexpr uint8 usageCount = 10; /**< Buffer usage type count. */

	/**
	 * @brief Buffer memory copy region description.
	 * @details See the @ref Buffer::copy().
	 */
	struct CopyRegion final
	{
		uint64 size = 0;      /**< Size of the region in bytes. */
		uint64 srcOffset = 0; /**< Source buffer offset of the region in bytes. */
		uint64 dstOffset = 0; /**< Destination buffer offset of the region in bytes. */
	};

	/**
	 * @brief Buffer memory barrier state.
	 */
	struct BarrierState final
	{
		uint32 access = 0;
		uint32 stage = 0;
	};
private:
	uint8 _alignment = 0;
	uint8* map = nullptr;
	uint64 deviceAddress = 0;
	Usage usage = {};
	BarrierState barrierState = {};

	Buffer(Usage usage, CpuAccess cpuAccess, Location location, Strategy strategy, uint64 size, uint64 version);
	Buffer(Usage usage, CpuAccess cpuAccess, Location location, Strategy strategy, uint64 version) noexcept :
		Memory(0, cpuAccess, location, strategy, version), usage(usage) { }
	bool destroy() final;

	friend class BufferExt;
	friend class LinearPool<Buffer>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty buffer data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access buffers.
	 */
	Buffer() = default;

	/**
	 * @brief Returns buffer usage flags.
	 * @details Buffer usage flags helps to optimize it usage inside the driver.
	 */
	Usage getUsage() const noexcept { return usage; }

	/**
	 * @brief Returns pointer to the buffer mapped memory or nullptr.
	 * @warning Use it only according to the @ref Memory::Access!
	 */
	uint8* getMap() noexcept { return map; }
	/**
	 * @brief Returns constant pointer to the buffer mapped memory or nullptr.
	 * @warning Use it only according to the @ref Memory::Access!
	 */
	const uint8* getMap() const noexcept { return map; }
	/**
	 * @brief Returns buffer device address which can be used inside shaders.
	 * @warning Make sure that your target GPU supports buffer device address!
	 */
	uint64 getDeviceAddress() const noexcept { return deviceAddress; }

	/**
	 * @brief Is buffer memory mapped. (Can be written or read)
	 * @details Buffer memory can not be accessed if it is not mapped.
	 */
	bool isMappable() const;
	/**
	 * @brief Invalidates buffer memory.
	 * @warning Always invalidate buffer memory before reading!
	 * 
	 * @param size memory region size (0 = full buffer size)
	 * @param offset memory region offset
	 * 
	 * @throw GardenError if failed to invalidate buffer memory.
	 */
	void invalidate(uint64 size = 0, uint64 offset = 0);
	/**
	 * @brief Flushes buffer memory.
	 * @warning Always flush buffer memory before using it for rendering!
	 * 
	 * @param size memory region size (0 = full buffer size)
	 * @param offset memory region offset
	 * 
	 * @throw GardenError if failed to flush buffer memory.
	 */
	void flush(uint64 size = 0, uint64 offset = 0);

	/*******************************************************************************************************************
	 * @brief Writes data to the buffer.
	 * 
	 * @param[in] data target buffer data
	 * @param size data size in bytes (0 = full buffer size)
	 * @param offset offset in the buffer in bytes
	 * 
	 * @throw GardenError if failed to map buffer memory.
	 */
	void writeData(const void* data, uint64 size = 0, uint64 offset = 0);
	/**
	 * @brief Writes data to the buffer.
	 * 
	 * @tparam T array element type
	 * @param[in] data target buffer data
	 * @param count array element count (0 = full array size)
	 * @param arrayOffset offset of the element in the array
	 * @param bufferOffset offset in the buffer in elements
	 * 
	 * @throw GardenError if failed to map buffer memory.
	 */
	template<typename T = float>
	void writeData(const vector<T>& data, psize count = 0, psize arrayOffset = 0, uint64 bufferOffset = 0)
	{
		if (count == 0)
		{
			return writeData(data.data() + arrayOffset,
				(data.size() - arrayOffset) * sizeof(T), bufferOffset * sizeof(T));
		}
		else
		{
			GARDEN_ASSERT(count + arrayOffset <= data.size());
			return writeData(data.data() + arrayOffset, count * sizeof(T), bufferOffset * sizeof(T));
		}
	}
	/**
	 * @brief Writes data to the buffer.
	 * 
	 * @tparam T array element type
	 * @tparam S size of the array
	 * @param[in] data target buffer data
	 * @param count array element count (0 = full array size)
	 * @param arrayOffset offset of the element in the array
	 * @param bufferOffset offset in the buffer in elements
	 * 
	 * @throw GardenError if failed to map buffer memory.
	 */
	template<typename T = float, psize S>
	void writeData(const array<T, S>& data, psize count = 0, psize arrayOffset = 0, uint64 bufferOffset = 0)
	{
		if (count == 0)
			return writeData(data.data() + arrayOffset, (data.size() - arrayOffset) * sizeof(T));
		else
		{
			GARDEN_ASSERT(count + arrayOffset <= data.size());
			return writeData(data.data() + arrayOffset, count * sizeof(T));
		}
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets buffer debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Fills buffer with 4 byte data. (clears)
	 * 
	 * @details
	 * The size must be either a multiple of 4, or 0 to fill the range from offset to the end of the buffer. If 0 size 
	 * is used and the remaining size of the buffer is not a multiple of 4, then the nearest smaller multiple is used.
	 * 
	 * @param data target 4 byte value
	 * @param size region fill size in bytes (0 = full buffer size)
	 * @param offset region offset in bytes
	 */
	void fill(uint32 value, uint64 size = 0, uint64 offset = 0);

	/**
	 * @brief Copies data regions from the source buffer to the destination.
	 * @details Fundamental operation used to copy data from one buffer to another within the GPU's memory.
	 * 
	 * @param source source buffer
	 * @param destination destination buffer
	 * @param[in] regions target memory regions
	 * @param count region array size
	 */
	static void copy(ID<Buffer> source, ID<Buffer> destination, const CopyRegion* regions, uint32 count);

	/**
	 * @brief Copies data regions from the source buffer to the destination.
	 * @details See the @ref Buffer::copy().
	 * 
	 * @tparam N array size
	 * @param source source buffer
	 * @param destination destination buffer
	 * @param[in] regions target memory regions
	 */
	template<psize N>
	static void copy(ID<Buffer> source, ID<Buffer> destination, const array<CopyRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	/**
	 * @brief Copies data regions from the source buffer to the destination.
	 * @details See the @ref Buffer::copy().
	 * 
	 * @param source source buffer
	 * @param destination destination buffer
	 * @param[in] regions target memory regions
	 */
	static void copy(ID<Buffer> source, ID<Buffer> destination, const vector<CopyRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	/**
	 * @brief Copies data region from the source buffer to the destination.
	 * @details See the @ref Buffer::copy().
	 * 
	 * @param source source buffer
	 * @param destination destination buffer
	 * @param[in] region target memory region
	 */
	static void copy(ID<Buffer> source, ID<Buffer> destination, const CopyRegion& region)
	{ copy(source, destination, &region, 1); }
	/**
	 * @brief Copies all data from the source buffer to the destination.
	 * @details See the @ref Buffer::copy().
	 * @note Source and destination buffer sizes should be the same.
	 * 
	 * @param source source buffer
	 * @param destination destination buffer
	 */
	static void copy(ID<Buffer> source, ID<Buffer> destination)
	{ CopyRegion region; copy(source, destination, &region, 1); }

	// TODO: Add support of self copying if regions not overlapping.
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Buffer::Usage)

/***********************************************************************************************************************
 * @brief Returns buffer usage name string.
 * @param imageUsage target buffer usage flags
 */
static string_view toString(Buffer::Usage bufferUsage)
{
	if (hasOneFlag(bufferUsage, Buffer::Usage::TransferSrc)) return "TransferSrc";
	if (hasOneFlag(bufferUsage, Buffer::Usage::TransferDst)) return "TransferDst";
	if (hasOneFlag(bufferUsage, Buffer::Usage::Vertex)) return "Vertex";
	if (hasOneFlag(bufferUsage, Buffer::Usage::Index)) return "Index";
	if (hasOneFlag(bufferUsage, Buffer::Usage::Uniform)) return "Uniform";
	if (hasOneFlag(bufferUsage, Buffer::Usage::Storage)) return "Storage";
	if (hasOneFlag(bufferUsage, Buffer::Usage::Indirect)) return "Indirect";
	if (hasOneFlag(bufferUsage, Buffer::Usage::DeviceAddress)) return "DeviceAddress";
	if (hasOneFlag(bufferUsage, Buffer::Usage::StorageAS)) return "StorageAS";
	if (hasOneFlag(bufferUsage, Buffer::Usage::BuildInputAS)) return "BuildInputAS";
	if (hasOneFlag(bufferUsage, Buffer::Usage::SBT)) return "SBT";
	return "None";
}
/**
 * @brief Returns buffer usage name string list.
 * @param imageUsage target buffer usage flags
 */
static string toStringList(Buffer::Usage bufferUsage)
{
	string list;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::None)) list += "None | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::TransferSrc)) list += "TransferSrc | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::TransferDst)) list += "TransferDst | " ;
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Vertex)) list += "Vertex | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Index)) list += "Index | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Uniform)) list += "Uniform | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Storage)) list += "Storage | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::Indirect)) list += "Indirect | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::DeviceAddress)) list += "DeviceAddress | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::StorageAS)) list += "StorageAS | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::BuildInputAS)) list += "BuildInputAS | ";
	if (hasAnyFlag(bufferUsage, Buffer::Usage::SBT)) list += "SBT | ";
	if (list.length() >= 3) list.resize(list.length() - 3);
	return list;
}

/***********************************************************************************************************************
 * @brief Graphics buffer resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class BufferExt final
{
public:
	/**
	 * @brief Returns buffer memory map.
	 * @warning In most cases you should use @ref Buffer functions.
	 * @param[in] buffer target buffer instance
	 */
	static uint8*& getMap(Buffer& buffer) noexcept { return buffer.map; }
	/**
	 * @brief Returns buffer device address.
	 * @warning In most cases you should use @ref Buffer functions.
	 * @param[in] buffer target buffer instance
	 */
	static uint64& getDeviceAddress(Buffer& buffer) noexcept { return buffer.deviceAddress; }
	/**
	 * @brief Returns buffer usage flags.
	 * @warning In most cases you should use @ref Buffer functions.
	 * @param[in] buffer target buffer instance
	 */
	static Buffer::Usage& getUsage(Buffer& buffer) noexcept { return buffer.usage; }
	/**
	 * @brief Returns buffer memory barrier state.
	 * @warning In most cases you should use @ref Buffer functions.
	 * @param[in] buffer target buffer instance
	 */
	static Buffer::BarrierState& getBarrierState(Buffer& buffer) noexcept { return buffer.barrierState; }

	/**
	 * @brief Creates a new buffer data.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * @param size buffer size in bytes
	 * @param version buffer instance version
	 */
	static Buffer create(Buffer::Usage usage, Buffer::CpuAccess cpuAccess,
		Buffer::Location location, Buffer::Strategy strategy, uint64 size, uint64 version)
	{
		return Buffer(usage, cpuAccess, location, strategy, size, version);
	}
	/**
	 * @brief Creates a new buffer data holder.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * @param version buffer instance version
	 */
	static Buffer create(Buffer::Usage usage, Buffer::CpuAccess cpuAccess,
		Buffer::Location location, Buffer::Strategy strategy, uint64 version)
	{
		return Buffer(usage, cpuAccess, location, strategy, version);
	}
	/**
	 * @brief Moves internal buffer objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source buffer instance
	 * @param[in,out] destination destination buffer instance
	 */
	static void moveInternalObjects(Buffer& source, Buffer& destination) noexcept
	{
		MemoryExt::getAllocation(destination) = MemoryExt::getAllocation(source);
		MemoryExt::getBinarySize(destination) = MemoryExt::getBinarySize(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		BufferExt::getMap(destination) = BufferExt::getMap(source);
		ResourceExt::getInstance(source) = nullptr;
	}
	/**
	 * @brief Destroys buffer instance.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * @param[in,out] buffer target buffer instance
	 */
	static void destroy(Buffer& buffer) { buffer.destroy(); }
};

/**
 * @brief Returns buffer channel binary size in bytes.
 * @param channel target buffer channel
 */
static psize toBinarySize(BufferChannel channel) noexcept
{
	switch (channel)
	{
	case BufferChannel::Positions: return sizeof(float3);
	case BufferChannel::Normals: return sizeof(float3);
	case BufferChannel::Tangents: return sizeof(float3);
	case BufferChannel::Bitangents: return sizeof(float3);
	case BufferChannel::TextureCoords: return sizeof(float2);
	case BufferChannel::VertexColors: return sizeof(float4);
	default: abort();
	}
}
/**
 * @brief Returns buffer channels binary size in bytes.
 * @param[in] channels target buffer channels
 */
static psize toBinarySize(const vector<BufferChannel>& channels) noexcept
{
	psize binarySize = 0;
	for (auto channel : channels)
		binarySize += toBinarySize(channel);
	return binarySize;
}

} // namespace garden::graphics
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
#include "linear-pool.hpp"
#include "garden/graphics/common.hpp"
#include "garden/graphics/memory.hpp"

namespace garden::graphics
{

using namespace ecsm;
class BufferExt;

//--------------------------------------------------------------------------------------------------
class Buffer final : public Memory
{
public:
	enum class Bind : uint8
	{
		None = 0x00, TransferSrc = 0x01, TransferDst = 0x02,
		Vertex = 0x04, Index = 0x08, Uniform = 0x10, Storage = 0x20
	};

	struct CopyRegion final
	{
		uint64 size = 0;
		uint64 srcOffset = 0;
		uint64 dstOffset = 0;
	};
private:
	Bind bind = {};
	uint16 _alignment = 0;
	uint8* map = nullptr;
	
	Buffer() = default;
	Buffer(Bind bind, Usage usage, uint64 size, uint64 version);
	Buffer(Bind bind, Usage usage, uint64 version) :
		Memory(0, usage, version) { this->bind = bind; }
	bool destroy() final;

	friend class Image;
	friend class Vulkan;
	friend class Pipeline;
	friend class BufferExt;
	friend class DescriptorSet;
	friend class CommandBuffer;
	friend class GraphicsPipeline;
	friend class LinearPool<Buffer>;
//--------------------------------------------------------------------------------------------------
public:
	Bind getBind() const noexcept { return bind; }

	uint8* getMap() noexcept { return map; }
	const uint8* getMap() const noexcept { return map; }

	// Note: invalidate before reading map.
	void invalidate(uint64 size = 0, uint64 offset = 0);
	// Note: flush before using buffer.
	void flush(uint64 size = 0, uint64 offset = 0);

	void setData(const void* data, uint64 size = 0, uint64 offset = 0);
	template<typename T = float>
	void setData(const vector<T>& data, psize count = 0, psize offset = 0)
	{
		if (count == 0)
		{
			return setData(data.data() + offset, (data.size() - offset) * sizeof(T));
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return setData(data.data() + offset, count * sizeof(T));
		}
	}
	template<typename T = float, psize S>
	void setData(const array<T, S>& data, psize count = 0, psize offset = 0)
	{
		if (count == 0)
		{
			return setData(data.data() + offset, (data.size() - offset) * sizeof(T));
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return setData(data.data() + offset, count * sizeof(T));
		}
	}

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	// Note: Size and offset are binary.
	void fill(uint32 data, uint64 size = 0, uint64 offset = 0);

	// TODO: add support of self copying if regions not overlapping.
	static void copy(ID<Buffer> source, ID<Buffer> destination,
		const CopyRegion* regions, uint32 count);

	template<psize N>
	static void copy(ID<Buffer> source, ID<Buffer> destination,
		const array<CopyRegion, N>& regions)
	{ copy(source, destination, regions.data(), (uint32)N); }
	static void copy(ID<Buffer> source, ID<Buffer> destination,
		const vector<CopyRegion>& regions)
	{ copy(source, destination, regions.data(), (uint32)regions.size()); }
	static void copy(ID<Buffer> source, ID<Buffer> destination,
		const CopyRegion& region)
	{ copy(source, destination, &region, 1); }
	static void copy(ID<Buffer> source, ID<Buffer> destination)
	{ CopyRegion region; copy(source, destination, &region, 1); }
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Buffer::Bind)

//--------------------------------------------------------------------------------------------------
static string_view toString(Buffer::Bind bufferBind)
{
	if (hasOneFlag(bufferBind, Buffer::Bind::None)) return "None";
	if (hasOneFlag(bufferBind, Buffer::Bind::TransferSrc)) return "TransferSrc";
	if (hasOneFlag(bufferBind, Buffer::Bind::TransferDst)) return "TransferDst";
	if (hasOneFlag(bufferBind, Buffer::Bind::Vertex)) return "Vertex";
	if (hasOneFlag(bufferBind, Buffer::Bind::Index)) return "Index";
	if (hasOneFlag(bufferBind, Buffer::Bind::Uniform)) return "Uniform";
	if (hasOneFlag(bufferBind, Buffer::Bind::Storage)) return "Storage";
	throw runtime_error("Unknown buffer bind type. (" + to_string((int)bufferBind) + ")");
}
static string toStringList(Buffer::Bind bufferBind)
{
	string list;
	if (hasAnyFlag(bufferBind, Buffer::Bind::None)) list += "None | ";
	if (hasAnyFlag(bufferBind, Buffer::Bind::TransferSrc)) list += "TransferSrc | ";
	if (hasAnyFlag(bufferBind, Buffer::Bind::TransferDst)) list += "TransferDst | " ;
	if (hasAnyFlag(bufferBind, Buffer::Bind::Vertex)) list += "Vertex | ";
	if (hasAnyFlag(bufferBind, Buffer::Bind::Index)) list += "Index | ";
	if (hasAnyFlag(bufferBind, Buffer::Bind::Uniform)) list += "Uniform | ";
	if (hasAnyFlag(bufferBind, Buffer::Bind::Storage)) list += "Storage | ";
	list.resize(list.length() - 3);
	return list;
}

//--------------------------------------------------------------------------------------------------
class BufferExt final
{
public:
	static Buffer::Bind& getBind(Buffer& buffer) noexcept { return buffer.bind; }
	static uint8*& getMap(Buffer& buffer) noexcept { return buffer.map; }

	static Buffer create(Buffer::Bind bind, Buffer::Usage usage, uint64 size, uint64 version)
	{
		return Buffer(bind, usage, size, version);
	}
	static Buffer create(Buffer::Bind bind, Buffer::Usage usage, uint64 version)
	{
		return Buffer(bind, usage, version);
	}
	static void moveInternalObjects(Buffer& source, Buffer& destination) noexcept
	{
		MemoryExt::getAllocation(destination) = MemoryExt::getAllocation(source);
		MemoryExt::getBinarySize(destination) = MemoryExt::getBinarySize(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		ResourceExt::getInstance(source) = nullptr;
	}
	static void destroy(Buffer& buffer) { buffer.destroy(); }
};

} // garden::graphics
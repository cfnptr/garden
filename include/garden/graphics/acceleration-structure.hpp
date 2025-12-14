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
 * @brief Common graphics ray tracing acceleration structure functions.
 */

#pragma once
#include "garden/graphics/buffer.hpp"

namespace garden::graphics
{

using namespace ecsm;
class AccelerationStructureExt;

/**
 * @brief Ray tracing acceleration structure build flags.
 * @details
 *
 * Build flags best practices:
 *   PreferFastBuild: Fully dynamic geometry like particles, destruction, changing prim counts or moving wildly.
 *   PreferFastBuild + AllowUpdate: Lower LOD dynamic objects, unlikely to be hit by too many rays.
 *   PreferFastTrace: Default choice for static level geometry.
 *   PreferFastTrace + AllowUpdate: Hero character, high-LOD dynamic objects, expected to be hit by many rays.
 */
enum class BuildFlagsAS : uint8
{
	None            = 0x00, /**< No acceleration structure build flags specified, zero mask. */
	ComputeQ        = 0x01, /**< Allows to use AS in the compute command buffer. */
	AllowUpdate     = 0x02, /**< Allows to update acceleration structure geometry positions. */
	AllowCompaction = 0x04, /**< Allows to compact acceleration structure storage. */
	PreferFastTrace = 0x08, /**< Prioritize trace performance over AS build time. */
	PreferFastBuild = 0x10, /**< Prioritize AS build time over trace performance. */
	PreferLowMemory = 0x20  /**< Minimize memory usage at expense of AS build time and trace performance. */
};

constexpr uint8 asBuildFlagCount = 5; /**< Ray tracing acceleration structure build flag count. */

/**
 * @brief Ray tracing acceleration structure base class.
 */
class AccelerationStructure : public Resource
{
public:
	/**
	 * @brief Acceleration structure types.
	 */
	enum class Type : uint8
	{
		Blas,  /**< Bottom level acceleration structure type. (Ray Tracing) */
		Tlas,  /**< Top level acceleration structure type. (Ray Tracing) */
		Count, /**< Acceleration structure type count. (Ray Tracing) */
	};

	/**
	 * @brief Acceleration structure compact data.
	 */
	struct CompactData final
	{
		vector<uint64> queryResults;
		void* queryPool = nullptr;
		uint32 queryPoolRef = 0;
	};
	/**
	 * @brief Acceleration structure build data array header.
	 */
	struct BuildDataHeader final
	{
		uint64 scratchSize = 0;
		uint32 geometryCount = 0;
		uint32 bufferCount = 0;
		CompactData* compactData = nullptr;
		uint32 queryPoolIndex = 0;
	};
protected:
	ID<Buffer> storageBuffer = {};
	uint64 deviceAddress = 0;
	void* buildData = nullptr;
	Buffer::BarrierState barrierState = {};
	uint32 geometryCount = 0;
	Type type = {};
	BuildFlagsAS flags = {};

	AccelerationStructure(uint32 geometryCount, BuildFlagsAS flags, Type type) noexcept : 
		geometryCount(geometryCount), type(type), flags(flags) { }
	bool destroy() final;

	friend class AccelerationStructureExt;
 public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty ray tracing acceleration structure.
	 * @note Use @ref GraphicsSystem to create, destroy and access acceleration structures.
	 */
	AccelerationStructure() noexcept = default;

	/**
	 * @brief Returns acceleration structure type.
	 */
	Type getType() const noexcept { return type; }
	/**
	 * @brief Returns acceleration structure build flags.
	 */
	BuildFlagsAS getFlags() const noexcept { return flags; }
	/**
	 * @brief Returns acceleration structure geometry buffer size.
	 */
	uint32 getGeometryCount() const noexcept { return geometryCount; }
	/**
	 * @brief Returns acceleration structure storage buffer instance.
	 */
	ID<Buffer> getStorageBuffer() const noexcept { return storageBuffer; }
	/**
	 * @brief Returns true if acceleration structure storage is ready for rendering.
	 */
	bool isStorageReady() const noexcept;

	/**
	 * @brief Returns acceleration structure scratch buffer size.
	 */
	uint64 getScratchSize() const noexcept
	{
		GARDEN_ASSERT_MSG(buildData, "Acceleration structure already built");
		auto header = (const BuildDataHeader*)buildData;
		return header->scratchSize;
	}

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Actually builds acceleration structure.
	 * @param scratchBuffer AS scratch buffer (null = auto temporary)
	 */
	virtual void build(ID<Buffer> scratchBuffer = {});

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets acceleration structure debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif

	static void _createVkInstance(uint64 size, uint8 type, BuildFlagsAS flags, 
		ID<Buffer>& storageBuffer, void*& instance, uint64& deviceAddress);
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(BuildFlagsAS)

/***********************************************************************************************************************
 * @brief Returns ray tracing acceleration structure build flags name string.
 * @param asBuildFlags target ray tracing acceleration structure build flag
 */
static string_view toString(BuildFlagsAS asBuildFlags)
{
	if (hasOneFlag(asBuildFlags, BuildFlagsAS::AllowUpdate)) return "AllowUpdate";
	if (hasOneFlag(asBuildFlags, BuildFlagsAS::AllowCompaction)) return "AllowCompaction";
	if (hasOneFlag(asBuildFlags, BuildFlagsAS::PreferFastTrace)) return "PreferFastTrace";
	if (hasOneFlag(asBuildFlags, BuildFlagsAS::PreferFastBuild)) return "PreferFastBuild";
	if (hasOneFlag(asBuildFlags, BuildFlagsAS::PreferLowMemory)) return "PreferLowMemory";
	return "None";
}
/**
 * @brief Returns ray tracing acceleration structure build flags name string list.
 * @param asBuildFlags target ray tracing acceleration structure build flags
 */
static string toStringList(BuildFlagsAS asBuildFlags)
{
	string list;
	if (hasAnyFlag(asBuildFlags, BuildFlagsAS::AllowUpdate)) list += "AllowUpdate | ";
	if (hasAnyFlag(asBuildFlags, BuildFlagsAS::AllowCompaction)) list += "AllowCompaction | ";
	if (hasAnyFlag(asBuildFlags, BuildFlagsAS::PreferFastTrace)) list += "PreferFastTrace | ";
	if (hasAnyFlag(asBuildFlags, BuildFlagsAS::PreferFastBuild)) list += "PreferFastBuild | ";
	if (hasAnyFlag(asBuildFlags, BuildFlagsAS::PreferLowMemory)) list += "PreferLowMemory | ";
	if (list.length() >= 3) list.resize(list.length() - 3);
	else return "None";
	return list;
}

/***********************************************************************************************************************
 * @brief Graphics acceleration structure resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class AccelerationStructureExt final
{
public:
	/**
	 * @brief Returns acceleration structure storage buffer.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static ID<Buffer>& getStorageBuffer(AccelerationStructure& as) noexcept { return as.storageBuffer; }
	/**
	 * @brief Returns acceleration structure device address.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static uint64& getDeviceAddress(AccelerationStructure& as) noexcept { return as.deviceAddress; }
	/**
	 * @brief Returns acceleration structure build data allocation.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static void*& getBuildData(AccelerationStructure& as) noexcept { return as.buildData; }
	/**
	 * @brief Returns acceleration structure memory barrier state.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static Buffer::BarrierState& getBarrierState(AccelerationStructure& as) noexcept { return as.barrierState; }
	/**
	 * @brief Returns acceleration structure type.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static AccelerationStructure::Type& getType(AccelerationStructure& as) noexcept { return as.type; }
	/**
	 * @brief Returns acceleration structure build flags.
	 * @warning In most cases you should use @ref AccelerationStructure functions.
	 * @param[in] as target acceleration structure instance
	 */
	static BuildFlagsAS& getFlags(AccelerationStructure& as) noexcept { return as.flags; }
};

} // namespace garden::graphics
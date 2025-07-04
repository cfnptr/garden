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
 * @brief Ray tracing top level acceleration structure (TLAS) functions.
 */

#pragma once
#include "garden/graphics/acceleration-structure/blas.hpp"

namespace garden::graphics
{

using namespace ecsm;
class TlasExt;

/**
 * @brief Ray tracing top level acceleration structure. (TLAS)
 */
class Tlas final : public AccelerationStructure
{
public:
	/**
	 * @brief TLAS instance flag types.
	 */
	enum class InstanceFlags : uint8
	{
		None           = 0x00, /**< No TLAS instance flags specified, zero mask. */
		DisableCulling = 0x01, /**< Disables face culling for this TLAS instance. */
		FlipFacing     = 0x02, /**< Facing determination for geometry in this instance is inverted. */
		ForceOpaque    = 0x04, /**< Forces all TLAS instance geometry opaque flag. */
		ForceNoOpaque  = 0x08  /**< Forces all TLAS instance geometry no opaque flag. */
	};

	/**
	 * @brief Tlas instance data container. (One BLAS)
	 */
	struct InstanceData final
	{
		float transform[3 * 4];
		ID<Blas> blas = {};
		uint32 customIndex;
		uint32 sbtRecordOffset;
		uint8 mask = 0;
		InstanceFlags flags = {};

		/**
		 * @brief Creates a new TLAS instance data container.
		 * 
		 * @param model instance model matrix
		 * @param blas target BLAS instance
		 * @param customIndex custom instance index
		 * @param sbtRecordOffset shader binding table record offset
		 * @param mask instance visibility mask
		 * @param flags instance flags
		 */
		InstanceData(const f32x4x4& model, ID<Blas> blas, uint32 customIndex, 
			uint32 sbtRecordOffset, uint8 mask, InstanceFlags flags) noexcept;
		InstanceData() noexcept = default;
	};
private:
	uint16 _alignment = 0;
	vector<InstanceData> instances;

	Tlas(vector<InstanceData>&& instances, ID<Buffer> instanceBuffer, BuildFlagsAS flags);

	friend class TlasExt;
	friend class LinearPool<Tlas>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty ray tracing top level acceleration structure. (TLAS)
	 * @note Use @ref GraphicsSystem to create, destroy and access TLAS'es.
	 */
	Tlas() = default;

	/**
	 * @brief Returns TLAS instance array.
	 */
	const vector<InstanceData>& getInstances() const noexcept { return instances; }

	/**
	 * @brief Returns TLAS buffer instance size in bytes.
	 */
	static uint32 getInstanceSize() noexcept;
	/**
	 * @brief Fills up TLAS instance buffer data.
	 * 
	 * @param[in] instanceArray target TLAS instance array
	 * @param instanceCount instance array size
	 * @param[out] data output instance buffer data
	 */
	static void getInstanceData(const InstanceData* instanceArray, uint32 instanceCount, uint8* data) noexcept;

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Actually builds top level acceleration structure.
	 * @param scratchBuffer AS scratch buffer (null = auto temporary)
	 */
	void build(ID<Buffer> scratchBuffer = {}) final;

	// TODO: add TLAS compaction if needed.
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(Tlas::InstanceFlags)

/***********************************************************************************************************************
 * @brief Returns TLAS instance flag name string.
 * @param tlasInstanceFlag target TLAS instance flag
 */
static string_view toString(Tlas::InstanceFlags tlasInstanceFlag)
{
	if (hasOneFlag(tlasInstanceFlag, Tlas::InstanceFlags::DisableCulling)) return "DisableCulling";
	if (hasOneFlag(tlasInstanceFlag, Tlas::InstanceFlags::FlipFacing)) return "FlipFacing";
	if (hasOneFlag(tlasInstanceFlag, Tlas::InstanceFlags::ForceOpaque)) return "ForceOpaque";
	if (hasOneFlag(tlasInstanceFlag, Tlas::InstanceFlags::ForceNoOpaque)) return "ForceNoOpaque";
	return "None";
}
/**
 * @brief Returns TLAS instance flags name string list.
 * @param tlasInstanceFlags target TLAS instance flags
 */
static string toStringList(Tlas::InstanceFlags tlasInstanceFlags)
{
	string list;
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::None)) list += "None | ";
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::DisableCulling)) list += "DisableCulling | ";
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::FlipFacing)) list += "FlipFacing | ";
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::ForceOpaque)) list += "ForceOpaque | ";
	if (hasAnyFlag(tlasInstanceFlags, Tlas::InstanceFlags::ForceNoOpaque)) list += "ForceNoOpaque | ";
	if (list.length() >= 3) list.resize(list.length() - 3);
	return list;
}

/***********************************************************************************************************************
 * @brief Graphics TLAS resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class TlasExt final
{
public:
	/**
	 * @brief Returns TLAS instance array.
	 * @warning In most cases you should use @ref Tlas functions.
	 * @param[in] tlas target TLAS instance
	 */
	static vector<Tlas::InstanceData>& getInstances(Tlas& tlas) noexcept { return tlas.instances; }

	/**
	 * @brief Creates a new TLAS data holder.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in] instances TLAS instance array
	 * @param instanceBuffer target TLAS instance buffer
	 * @param flags acceleration structure build flags
	 */
	static Tlas create(vector<Tlas::InstanceData>&& instances, 
		ID<Buffer> instanceBuffer, BuildFlagsAS flags)
	{
		return Tlas(std::move(instances), instanceBuffer, flags);
	}
	/**
	 * @brief Destroys TLAS instance.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * @param[in,out] tlas target TLAS instance
	 */
	static void destroy(Tlas& tlas) { tlas.destroy(); }
};

} // namespace garden::graphics
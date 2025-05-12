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
		DisableCulling, /**< */
		Count
	};

	/**
	 * @brief Tlas instance data container. (One 3D model)
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
	};
private:
	Tlas(ID<Buffer> instanceBuffer, BuildFlagsAS flags);

	friend class TlasExt;
	friend class LinearPool<Tlas>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty ray tracing top level acceleration structure. (TLAS)
	 * @note Use @ref GraphicsSystem to create, destroy and access TLAS'es.
	 */
	Tlas() = default;

	/**
	 * @brief Returns TLAS buffer instance size in bytes.
	 */
	static uint32 getInstanceSize() noexcept;
	/**
	 * @brief Creates and returns TLAS instance buffer data.
	 * 
	 * @param[in] instanceArray target TLAS instance array
	 * @param instanceCount instance array size
	 * @param[out] data output instance buffer data
	 */
	static void getInstanceData(const InstanceData* instanceArray, uint32 instanceCount, uint8* data) noexcept;
};

} // namespace garden::graphics
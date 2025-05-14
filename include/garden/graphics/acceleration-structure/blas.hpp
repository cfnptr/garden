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
 * @brief Ray tracing bottom level acceleration structure (BLAS) functions.
 */

#pragma once
#include "garden/graphics/acceleration-structure.hpp"

namespace garden::graphics
{

using namespace ecsm;
class BlasExt;

/**
 * @brief Ray tracing bottom level acceleration structure. (BLAS)
 */
class Blas final : public AccelerationStructure
{
public:
	/**
	 * @brief Blas triangle data container. (One 3D model)
	 */
	struct TrianglesBuffer final
	{
		ID<Buffer> vertexBuffer = {};
		ID<Buffer> indexBuffer = {};
		uint32 vertexSize = 0;
		uint32 vertexCount = 0;
		uint32 vertexOffset = 0;
		uint32 primitiveCount = 0;
		uint32 primitiveOffset = 0;
		IndexType indexType = {};
		bool isOpaqueOnly = false;
	};
	/**
	 * @brief Blas AABB data container.
	 */
	struct AabbsBuffer final
	{
		ID<Buffer> aabbBuffer = {};
		uint32 aabbStride = 0;
		uint32 aabbCount = 0;
		uint32 aabbOffset = 0;
		bool isOpaqueOnly = false;
	};
private:
	Blas(const TrianglesBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags);
	Blas(const AabbsBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags);

	friend class BlasExt;
	friend class LinearPool<Blas>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty ray tracing bottom level acceleration structure. (BLAS)
	 * @note Use @ref GraphicsSystem to create, destroy and access BLAS'es.
	 */
	Blas() = default;

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Updates bottom level acceleration structure geometry positions.
	 * @warning Only positions can be updated! If changed geometry count you should rebuild BLAS instead.
	 */
	// TODO: void update(const TrianglesBuffer* geometryArray, uint32 geometryCount);
};

} // namespace garden::graphics
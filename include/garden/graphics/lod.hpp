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
 * @brief Common level of detail (LOD) functions.
 */

#pragma once
#include "garden/graphics/buffer.hpp"

namespace garden::graphics
{

/**
 * @brief LOD buffers container.
 */
class LodBuffer final
{
	vector<uint8> readyStates;
	vector<Ref<Buffer>> vertexBuffers;
	vector<Ref<Buffer>> indexBuffers;
	vector<float> splits;

	void destroy();
public:
	/**
	 * @brief Returns LOD ready state array.
	 */
	const vector<uint8>& getReadyStates() const noexcept { return readyStates; }
	/**
	 * @brief Returns LOD vertex buffer array.
	 */
	const vector<Ref<Buffer>>& getVertexBuffers() const noexcept { return vertexBuffers; }
	/**
	 * @brief Returns LOD index buffer array.
	 */
	const vector<Ref<Buffer>>& getIndexBuffers() const noexcept { return indexBuffers; }
	/**
	 * @brief Returns LOD split array.
	 */
	const vector<float>& getSplits() const noexcept { return splits; }

	/**
	 * @brief Returns total LOD level count.
	 */
	uint32 getLevelCount() const noexcept { return splits.size(); }
	/**
	 * @brief Returns true if LOD mesh is loaded and ready.
	 */
	bool isLevelReady(uint32 level) const noexcept { return readyStates[level] > 1; }

	/**
	 * @brief Returns mesh LOD based on the specified distance to the model and readiness.
	 * @return True if returned mesh is loaded and ready for rendering.
	 * 
	 * @param distanceSq target distance to the model (power of 2)
	 * @param[out] vertexBuffer mesh vertex buffer
	 * @param[out] indexBuffer mesh index buffer
	 */
	bool getLevel(float distanceSq, ID<Buffer>& vertexBuffer, ID<Buffer>& indexBuffer) const;

	/**
	 * @brief Inserts a new LOD mesh level.
	 * 
	 * @param level target LOD mesh level
	 * @param splitSq distance to the LOD level (power of 2)
	 * @param[in] vertexBuffer mesh vertex buffer
	 * @param[in] indexBuffer mesh index buffer
	 */
	void addLevel(uint32 level, float splitSq, const Ref<Buffer>& vertexBuffer, const Ref<Buffer>& indexBuffer);
	/**
	 * @brief Removes specified LOD mesh level.
	 * @param level target LOD mesh level
	 */
	void removeLevel(uint32 level);

	/**
	 * @brief Updates LOD buffers readiness at the specified level.
	 * 
	 * @param level target LOD buffers level
	 * @param loadedBuffer loaded buffer instance
	 */
	void updateReadyState(uint32 level, ID<Buffer> loadedBuffer);
};

} // namespace garden::graphics

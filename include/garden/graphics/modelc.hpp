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

/*******************************************************************************************************************
 * @file
 * @brief Common 3D model conversion functions.
 */

#pragma once
#include "garden/graphics/buffer.hpp"

namespace garden::graphics
{

/**
 * @brief Garden 3D model converter. (Uses Assimp internally)
 */
class ModelConverter final
{
public:
	/**
	 * @brief Loads 3D model data. (Vertices and indices)
	 *
	 * @param[in,out] data target shader data container
	 */
	// static void loadModel(GraphicsData& data);

	#if GARDEN_DEBUG || defined(GARDEN_MODEL_CONVERTER)
	/**
	 * @brief Converts specified 3D model to the Garden model format.
	 * 
	 * @param filePath target model to convert path
	 * @param inputPath input model directory path
	 * @param outputPath output model directory path
	 * 
	 * @return Returns true on success and writes model data, otherwise false.
	 */
	static bool convertModel(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath);
	#endif
};

} // namespace garden::graphics
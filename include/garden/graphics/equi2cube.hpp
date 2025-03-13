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
 * @brief Common equirectangular image to cubemap conversion functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "math/vector.hpp"

namespace garden::graphics
{

/**
 * @brief Equirectangular image to cubemap converter. 
 */
class Equi2Cube final
{
public:
	static void convert(uint3 coords, uint32 cubemapSize, uint2 equiSize,
		uint2 equiSizeMinus1, const f32x4* equiPixels, f32x4* cubePixels, float invDim);

	#if GARDEN_DEBUG || defined(EQUI2CUBE)
	/**
	 * @brief Converts input equirectangular image to cubemap. 
	 * 
	 * @param filePath target image to convert path
	 * @param inputPath input image directory path
	 * @param outputPath input images directory path
	 * 
	 * @return Returns true on success and writes cubemap images, otherwise false.
	 */
	static bool convertImage(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath);
	#endif
};

} // namespace garden::graphics
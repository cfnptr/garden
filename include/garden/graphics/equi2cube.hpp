// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/graphics/image.hpp"
#include "math/ibl.hpp"

namespace garden::graphics
{

/**
 * @brief Equirectangular image to cubemap converter. 
 */
class Equi2Cube final
{
public:
	static f32x4 filterCubeMap(float2 coords, const f32x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept;
	static Color filterCubeMap(float2 coords, const Color* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept;

	template<class T>
	static void convert(uint3 coords, uint32 cubemapSize, uint2 equiSize,
		uint2 equiSizeMinus1, const T* equiPixels, T* cubePixels, float invDim) noexcept
	{
		auto dir = ibl::coordsToDir(coords, invDim); auto uv = ibl::toSphericalMapUV(dir);
		cubePixels[coords.y * cubemapSize + coords.x] = filterCubeMap(
			uv * equiSize, equiPixels, equiSizeMinus1, equiSize.x);
	}
	template<class T>
	static void convert(T** cubeFaces, uint32 cubemapSize, uint2 equiSize,
		uint2 equiSizeMinus1, const T* equiPixels, float invDim) noexcept
	{
		for (uint32 face = 0; face < Image::cubemapFaceCount; face++)
		{
			auto cubePixels = cubeFaces[face];
			for (uint32 y = 0; y < cubemapSize; y++)
			{
				for (uint32 x = 0; x < cubemapSize; x++)
				{
					convert(uint3(x, y, face), cubemapSize, equiSize,
						equiSizeMinus1, equiPixels, cubePixels, invDim);
				}
			}
		}
	}

	static void writeExrImageData(const fs::path& filePath, uint32 size, 
		const vector<uint8>& data, Image::Format imageForamt, bool saveAs16);

	#if GARDEN_DEBUG || defined(EQUI2CUBE)
	/**
	 * @brief Converts input equirectangular image to cubemap. 
	 * 
	 * @param filePath target image to convert path
	 * @param inputPath input image directory path
	 * @param outputPath output images directory path
	 * 
	 * @return Returns false if failed to open image file.
	 * @throw GardenError on image conversion error.
	 */
	static bool convertImage(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath);
	#endif
};

} // namespace garden::graphics
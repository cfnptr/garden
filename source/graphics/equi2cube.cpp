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

#include "garden/graphics/equi2cube.hpp"
#include "garden/thread-pool.hpp"
#include "garden/file.hpp"
#include "math/ibl.hpp"

#define TINYEXR_IMPLEMENTATION
#include "garden/graphics/exr.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <atomic>
#include <iostream>

using namespace garden;
using namespace garden::graphics;
using namespace math::ibl;

#if GARDEN_DEBUG || defined(EQUI2CUBE)
//******************************************************************************************************************
// x = 1.0 / (Pi * 2), y =  1.0 / Pi
constexpr float2 INV_ATAN = float2(0.15915494309189533576f, 0.318309886183790671538f);

static float2 toSphericalMapUV(float3 v) noexcept
{
	auto st = float2(atan2(v.x, v.z), asin(-v.y));
	return fma(float2(st.x, st.y), INV_ATAN, float2(0.5f));
}
static f32x4 filterCubeMap(float2 coords, const f32x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
{
	auto coords0 = min((uint2)coords, sizeMinus1);
	auto coords1 = min(coords0 + uint2::one, sizeMinus1);
	auto uv = coords - coords0;
	auto invUV = 1.0f - uv;

	auto s0 = pixels[coords0.y * sizeX + coords0.x];
	auto s1 = pixels[coords0.y * sizeX + coords1.x];
	auto s2 = pixels[coords1.y * sizeX + coords0.x];
	auto s3 = pixels[coords1.y * sizeX + coords1.x];

	return s0 * (invUV.x * invUV.y) + s1 * (uv.x * invUV.y) +
		s2 * (invUV.x * uv.y) + s3 * (uv.x * uv.y);
}

void Equi2Cube::convert(uint3 coords, uint32 cubemapSize, uint2 equiSize,
	uint2 equiSizeMinus1, const f32x4* equiPixels, f32x4* cubePixels, float invDim) noexcept
{
	auto dir = coordsToDir(coords, invDim);
	auto uv = toSphericalMapUV(dir);

	cubePixels[coords.y * cubemapSize + coords.x] = filterCubeMap(
		uv * equiSize, equiPixels, equiSizeMinus1, equiSize.x);
}

static void writeExrImageData(const fs::path& filePath, uint32 size, const vector<uint8>& data)
{
	auto directory = filePath.parent_path();
	if (!fs::exists(directory))
		fs::create_directories(directory);

	const char* error = nullptr;
	auto result = SaveEXR((const float*)data.data(), size, size, 
		4, true, filePath.generic_string().c_str(), &error);

	if (result != TINYEXR_SUCCESS)
	{
		auto errorString = string(error);
		FreeEXRErrorMessage(error);
		throw GardenError("Failed to store EXR image. ("
			"path: " + filePath.generic_string() + ", "
			"error: " + errorString + ")");
	}
}

//******************************************************************************************************************
bool Equi2Cube::convertImage(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{	
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(!inputPath.empty());
	GARDEN_ASSERT(!outputPath.empty());

	auto path = inputPath / filePath;
	vector<uint8> dataBuffer, equiData; uint2 equiSize;
	float* pixels = nullptr; // Note: width * height * RGBA
	int sizeX = 0, sizeY = 0;

	if (!File::tryLoadBinary(path, dataBuffer))
		return false;

	auto extension = filePath.extension();
	if (extension == ".exr")
	{
		const char* error = nullptr;
		auto result = LoadEXRFromMemory(&pixels, &sizeX, &sizeY, dataBuffer.data(), dataBuffer.size(), &error);
		if (result == TINYEXR_ERROR_CANT_OPEN_FILE)
		{
			FreeEXRErrorMessage(error);
			return false;
		}

		if (result != TINYEXR_SUCCESS)
		{
			auto errorString = string(error);
			FreeEXRErrorMessage(error);
			throw GardenError("Failed to load EXR image. ("
				"path: " + filePath.generic_string() + ", "
				"error: " + errorString + ")");
		}
	}
	else if (extension == ".hdr")
	{
		pixels = stbi_loadf_from_memory(dataBuffer.data(),
			(int)dataBuffer.size(), &sizeX, &sizeY, nullptr, 4);
		if (!pixels)
			throw GardenError("Failed to load HDR image. (path: " + filePath.generic_string() + ")");
	}
	else
	{
		throw GardenError("Unsupported image file extension. ("
			"path: " + filePath.generic_string() + ")");
	}

	equiSize = uint2((uint32)sizeX, (uint32)sizeY);
	equiData.resize(sizeof(f32x4) * equiSize.x * equiSize.y);
	memcpy(equiData.data(), pixels, equiData.size());
	free(pixels);

	auto cubemapSize = equiSize.x / 4;
	if (equiSize.x / 2 != equiSize.y || cubemapSize % 32 != 0)
		throw GardenError("Image is not a cubemap. (path: " + filePath.generic_string() + ")");

	auto invDim = 1.0f / cubemapSize;
	auto equiSizeMinus1 = equiSize - 1u;
	auto equiPixels = (f32x4*)equiData.data();
	auto pixelsSize = sizeof(f32x4) * cubemapSize * cubemapSize;

	vector<uint8> left(pixelsSize), right(pixelsSize), bottom(pixelsSize),
		top(pixelsSize), back(pixelsSize), front(pixelsSize);

	f32x4* cubePixelArray[6] =
	{
		(f32x4*)right.data(), (f32x4*)left.data(),
		(f32x4*)top.data(), (f32x4*)bottom.data(),
		(f32x4*)front.data(), (f32x4*)back.data(),
	};

	for (uint32 face = 0; face < 6; face++)
	{
		auto cubePixels = cubePixelArray[face];
		for (uint32 y = 0; y < cubemapSize; y++)
		{
			for (uint32 x = 0; x < cubemapSize; x++)
			{
				convert(uint3(x, y, face), cubemapSize, equiSize,
					equiSizeMinus1, equiPixels, cubePixels, invDim);
			}
		}
	}

	auto cacheFilePath = (outputPath / filePath).generic_string();
	cacheFilePath.resize(cacheFilePath.length() - 4);
	writeExrImageData(cacheFilePath + "-nx.exr", cubemapSize, left);
	writeExrImageData(cacheFilePath + "-px.exr", cubemapSize, right);
	writeExrImageData(cacheFilePath + "-ny.exr", cubemapSize, bottom);
	writeExrImageData(cacheFilePath + "-py.exr", cubemapSize, top);
	writeExrImageData(cacheFilePath + "-nz.exr", cubemapSize, back);
	writeExrImageData(cacheFilePath + "-pz.exr", cubemapSize, front);
	return true;
}
#endif

#ifdef EQUI2CUBE
//******************************************************************************************************************
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "equi2cube: error: no image file name\n";
		return EXIT_FAILURE;
	}

	int logOffset = 1;
	fs::path workingPath = fs::path(argv[0]).parent_path();
	auto inputPath = workingPath, outputPath = workingPath;
	ThreadPool* threadPool = nullptr; atomic_int convertResult = true;
	
	for (int i = 1; i < argc; i++)
	{
		auto arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
		{
			cout << "(C) 2022-" GARDEN_CURRENT_YEAR " Nikita Fediuchin. All rights reserved.\n"
				"equi2cube - Equirectangular to cubemap image converter.\n"
				"\n"
				"Usage: equi2cube [options] name...\n"
				"\n"
				"Options:\n"
				"  -i <dir>      Read input from <dir>.\n"
				"  -o <dir>      Write output to <dir>.\n"
				"  -t <value>    Specify thread pool size. (Uses all cores by default)\n"
				"  -h            Display available options.\n"
				"  --help        Display available options.\n"
				"  --version     Display converter version information.\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "--version") == 0)
		{
			cout << "equi2cube " GARDEN_VERSION_STRING "\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "-i") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no input directory\n";
				return EXIT_FAILURE;
			}

			inputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no output directory\n";
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-t") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no thread count\n";
				return EXIT_FAILURE;
			}

			auto count = atoi(argv[i + 1]);
			if (count > 0 && count < thread::hardware_concurrency())
			{
				if (threadPool)
				{
					threadPool->wait();
					delete threadPool;
				}
				threadPool = new ThreadPool(false, "T", count);
			}
			logOffset += 2; i++;
		}
		else if (arg[0] == '-')
		{
			cout << "equi2cube: error: unsupported option: '" << arg << "'\n";
			return EXIT_FAILURE;
		}
		else
		{
			if (!threadPool)
				threadPool = new ThreadPool(false, "T");
			threadPool->addTask([=, &convertResult](const ThreadPool::Task& task)
			{
				if (!convertResult)
					return;

				// Note: Sending one batched message due to multithreading.
				cout << string("Converting ") + arg + "\n" << flush;

				auto result = Equi2Cube::convertImage(arg, inputPath, outputPath);
				if (!result)
					cout << string("equi2cube: error: no image file found (") + arg + ")\n";
				convertResult &= result;
			});
		}
	}

	if (threadPool)
	{
		threadPool->wait();
		delete threadPool;
	}
	return convertResult ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
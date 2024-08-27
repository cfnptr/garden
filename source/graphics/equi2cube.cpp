// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
#include "garden/file.hpp"
#include "math/ibl.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <vector>
#include <fstream>
#include <iostream>

using namespace std;
using namespace garden;
using namespace garden::graphics;
using namespace math::ibl;

#if GARDEN_DEBUG || defined(EQUI2CUBE)
//******************************************************************************************************************
// x = 1.0 / (Pi * 2), y =  1.0 / Pi
static const float2 INV_ATAN = float2(0.15915494309189533576f, 0.318309886183790671538f);

static float2 toSphericalMapUV(const float3& v)
{
	auto st = float2(atan2(v.x, v.z), asin(-v.y));
	return fma(float2(st.x, st.y), INV_ATAN, float2(0.5f));
}
static float4 filterCubeMap(float2 coords, const float4* pixels, int2 sizeMinus1, int32 sizeX)
{
	auto coords0 = int2((int32)coords.x, (int32)coords.y);
	coords0 = clamp(coords0, int2(0), sizeMinus1);
	auto coords1 = min(coords0 + 1, sizeMinus1);
	auto uv = coords - coords0;
	auto invUV = 1.0f - uv;

	auto s0 = pixels[coords0.y * sizeX + coords0.x];
	auto s1 = pixels[coords0.y * sizeX + coords1.x];
	auto s2 = pixels[coords1.y * sizeX + coords0.x];
	auto s3 = pixels[coords1.y * sizeX + coords1.x];

	return (invUV.x * invUV.y) * s0 + (uv.x * invUV.y) * s1 +
		(invUV.x * uv.y) * s2 + (uv.x * uv.y) * s3;
}

void Equi2Cube::convert(const int3& coords, int32 cubemapSize, int2 equiSize,
	int2 equiSizeMinus1, const float4* equiPixels, float4* cubePixels, float invDim)
{
	auto dir = coordsToDir(coords, invDim);
	auto uv = toSphericalMapUV(dir);

	cubePixels[coords.y * cubemapSize + coords.x] = filterCubeMap(
		uv * equiSize, equiPixels, equiSizeMinus1, equiSize.x);
}

static void writeImageData(const fs::path& filePath, int32 size, const vector<uint8>& data)
{
	auto directory = filePath.parent_path();
	if (!fs::exists(directory))
		fs::create_directories(directory);

	auto pathString = filePath.generic_string();
	stbi_write_hdr(pathString.c_str(), size, size, 4, (const float*)data.data());
}

//******************************************************************************************************************
bool Equi2Cube::convertImage(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{	
	GARDEN_ASSERT(!filePath.empty());
	vector<uint8> dataBuffer, equiData; int2 equiSize;
	if (!File::tryLoadBinary(inputPath / filePath, dataBuffer))
		return false;

	auto extension = filePath.extension();
	if (extension == ".hdr")
	{
		auto pixels = (uint8*)stbi_loadf_from_memory(dataBuffer.data(),
			(int)dataBuffer.size(), &equiSize.x, &equiSize.y, nullptr, 4);
		if (!pixels)
			throw runtime_error("Invalid image data.(path: " + filePath.generic_string() + ")");
		equiData.resize(sizeof(float4) * equiSize.x * equiSize.y);
		memcpy(equiData.data(), pixels, equiData.size());
		stbi_image_free(pixels);
	}
	else
	{
		throw runtime_error("Unsupported image file extension. ("
			"path: " + filePath.generic_string() + ")");
	}

	auto cubemapSize = equiSize.x / 4;
	if (equiSize.x / 2 != equiSize.y || cubemapSize % 32 != 0)
		throw runtime_error("Image is not a cubemap. (path: " + filePath.generic_string() + ")");

	auto invDim = 1.0f / cubemapSize;
	auto equiSizeMinus1 = equiSize - 1;
	auto equiPixels = (float4*)equiData.data();
	auto pixelsSize = sizeof(float4) * cubemapSize * cubemapSize;
	vector<uint8> left(pixelsSize), right(pixelsSize), bottom(pixelsSize),
		top(pixelsSize), back(pixelsSize), front(pixelsSize);
	auto size = int2(equiSize.x / 4, equiSize.y / 2);

	float4* cubePixelArray[6] =
	{
		(float4*)right.data(), (float4*)left.data(),
		(float4*)top.data(), (float4*)bottom.data(),
		(float4*)front.data(), (float4*)back.data(),
	};

	for (uint8 face = 0; face < 6; face++)
	{
		auto cubePixels = cubePixelArray[face];
		for (int32 y = 0; y < cubemapSize; y++)
		{
			for (int32 x = 0; x < cubemapSize; x++)
			{
				convert(int3(x, y, face), cubemapSize, equiSize,
					equiSizeMinus1, equiPixels, cubePixels, invDim);
			}
		}
	}

	auto cacheFilePath = (outputPath / filePath).generic_string();
	cacheFilePath.resize(cacheFilePath.length() - 4);
	writeImageData(cacheFilePath + "-nx.hdr", cubemapSize, left);
	writeImageData(cacheFilePath + "-px.hdr", cubemapSize, right);
	writeImageData(cacheFilePath + "-ny.hdr", cubemapSize, bottom);
	writeImageData(cacheFilePath + "-py.hdr", cubemapSize, top);
	writeImageData(cacheFilePath + "-nz.hdr", cubemapSize, back);
	writeImageData(cacheFilePath + "-pz.hdr", cubemapSize, front);
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
				"  -i <dir>     Read input from <dir>.\n"
				"  -o <dir>     Write output to <dir>.\n"
				"  -h           Display available options.\n"
				"  --help       Display available options.\n"
				"  --version    Display converter version information.\n";
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
			logOffset += 2;
			i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no output directory\n";
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1];
			logOffset += 2;
			i++;
		}
		else if (arg[0] == '-')
		{
			cout << "equi2cube: error: unsupported option: '" << arg << "'\n";
			return EXIT_FAILURE;
		}
		else
		{
			// TODO: do this from multiple thread.

			int progress = (int)(((float)((i - logOffset) + 1) /
				(float)(argc - logOffset)) * 100.0f);

			const char* spacing;
			if (progress < 10)
				spacing = "  ";
			else if (progress < 100)
				spacing = " ";
			else
				spacing = "";

			cout << "[" << spacing << progress << "%] " <<
				"Converting image " << arg << "\n" << flush;

			if (!Equi2Cube::convertImage(arg, inputPath, outputPath))
			{
				cout << "equi2cube: error: no image file found (" << arg << ")\n";
				return EXIT_FAILURE;
			}
		}
	}

	return EXIT_SUCCESS;
}
#endif
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

#include "garden/graphics/equi2cube.hpp"
#include "garden/thread-pool.hpp"
#include "garden/file.hpp"
#include "png.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <ImfRgbaFile.h>
#include <ImfArray.h>

#include <atomic>
#include <iostream>

using namespace garden;
using namespace garden::graphics;
using namespace math::ibl;

//**********************************************************************************************************************
namespace
{
	class ExrMemoryStream final : public Imf::IStream
	{
		vector<uint8> fileData;
		uint64 dataPos = 0;
	public:
		ExrMemoryStream(vector<uint8>&& fileData) : 
			Imf::IStream("memory"), fileData(std::move(fileData)) { }
		~ExrMemoryStream() override { }

		bool isMemoryMapped() const override { return true; }

		bool read(char c[/*n*/], int n) override
		{
			if (dataPos + n >= fileData.size())
				throw runtime_error("Out of EXR file bounds.");
			memcpy(c, fileData.data(), n); dataPos += n;
			return dataPos != fileData.size();
		}
		char* readMemoryMapped(int n) override
		{
			if (dataPos + n >= fileData.size())
				throw runtime_error("Out of EXR file bounds.");
			auto memory = (char*)fileData.data() + dataPos; dataPos += n;
			return memory;
		}

		uint64_t tellg() override { return dataPos; }
		void seekg(uint64_t pos) override { dataPos = pos; }
		void clear() override { }
		int64_t size() override { return (int64_t)fileData.size(); }
		bool isStatelessRead() const override { return true; }

		int64_t read(void* buf, uint64_t sz, uint64_t offset) override
		{
			if (offset >= fileData.size())
				return 0;
			auto remaining = fileData.size() - offset;
    		auto bytesToRead = (sz < remaining) ? sz : remaining;
			memcpy(buf, fileData.data() + offset, bytesToRead);
			return bytesToRead;
		}
	};
}

//**********************************************************************************************************************
static f32x4 imfToRgb(Imf::Rgba imf) noexcept { return f32x4(imf.r, imf.g, imf.b, imf.a); }
static Imf::Rgba rgbToImf(f32x4 rgb) noexcept { return Imf::Rgba(rgb.getX(), rgb.getY(), rgb.getZ(), rgb.getW()); }

static Imf::Rgba filterCubeMap(float2 coords, const Imf::Rgba* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
{
	auto coords0 = min((uint2)coords, sizeMinus1);
	auto coords1 = min(coords0 + uint2::one, sizeMinus1);
	auto uv = coords - coords0, invUV = 1.0f - uv;

	auto s0 = imfToRgb(pixels[coords0.y * sizeX + coords0.x]);
	auto s1 = imfToRgb(pixels[coords0.y * sizeX + coords1.x]);
	auto s2 = imfToRgb(pixels[coords1.y * sizeX + coords0.x]);
	auto s3 = imfToRgb(pixels[coords1.y * sizeX + coords1.x]);

	return rgbToImf(fma(s0, f32x4(invUV.x * invUV.y), fma(s1, f32x4(uv.x * invUV.y),
		fma(s2, f32x4(invUV.x * uv.y), s3 * (uv.x * uv.y)))));
}
static void convert(Imf::Rgba** cubeFaces, uint32 cubemapSize, uint2 equiSize,
	uint2 equiSizeMinus1, const Imf::Rgba* equiPixels, float invDim) noexcept
{
	for (uint32 face = 0; face < Image::cubemapFaceCount; face++)
	{
		auto cubePixels = cubeFaces[face];
		for (uint32 y = 0; y < cubemapSize; y++)
		{
			for (uint32 x = 0; x < cubemapSize; x++)
			{
				auto coords = uint3(x, y, face);
				auto dir = ibl::coordsToDir(coords, invDim);
				auto uv = ibl::toSphericalMapUV(dir);

				cubePixels[coords.y * cubemapSize + coords.x] = filterCubeMap(
					uv * equiSize, equiPixels, equiSizeMinus1, equiSize.x);
			}
		}
	}
}

#if GARDEN_DEBUG || defined(EQUI2CUBE)
//**********************************************************************************************************************
f32x4 Equi2Cube::filterCubeMap(float2 coords, const f32x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
{
	auto coords0 = min((uint2)coords, sizeMinus1);
	auto coords1 = min(coords0 + uint2::one, sizeMinus1);
	auto uv = coords - coords0, invUV = 1.0f - uv;

	auto s0 = pixels[coords0.y * sizeX + coords0.x];
	auto s1 = pixels[coords0.y * sizeX + coords1.x];
	auto s2 = pixels[coords1.y * sizeX + coords0.x];
	auto s3 = pixels[coords1.y * sizeX + coords1.x];

	return fma(s0, f32x4(invUV.x * invUV.y), fma(s1, f32x4(uv.x * invUV.y),
		fma(s2, f32x4(invUV.x * uv.y), s3 * (uv.x * uv.y))));
}
Color Equi2Cube::filterCubeMap(float2 coords, const Color* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
{
	auto coords0 = min((uint2)coords, sizeMinus1);
	auto coords1 = min(coords0 + uint2::one, sizeMinus1);
	auto uv = coords - coords0, invUV = 1.0f - uv;

	auto s0 = srgbToRgb((f32x4)pixels[coords0.y * sizeX + coords0.x]);
	auto s1 = srgbToRgb((f32x4)pixels[coords0.y * sizeX + coords1.x]);
	auto s2 = srgbToRgb((f32x4)pixels[coords1.y * sizeX + coords0.x]);
	auto s3 = srgbToRgb((f32x4)pixels[coords1.y * sizeX + coords1.x]);

	return (Color)rgbToSrgb(fma(s0, f32x4(invUV.x * invUV.y), fma(s1, 
		f32x4(uv.x * invUV.y), fma(s2, f32x4(invUV.x * uv.y), s3 * (uv.x * uv.y)))));
}

void Equi2Cube::writeExrImageData(const fs::path& filePath, uint32 size, 
	const vector<uint8>& data, Image::Format imageForamt, bool saveAs16)
{
	auto formatBinarySize = toBinarySize(imageForamt);
	if (formatBinarySize % 4 != 0)
		throw GardenError("Unsupported EXR image format for writing.");

	/* TODO: replace with OpenEXR
	const char* error = nullptr;
	auto result = SaveEXR((const float*)data.data(), size, size,
		formatBinarySize / 4, saveAs16, filePath.generic_string().c_str(), &error);

	if (result != TINYEXR_SUCCESS)
	{
		auto errorString = string(error); FreeEXRErrorMessage(error);
		throw GardenError("Failed to store EXR image. ("
			"path: " + filePath.generic_string() + ", error: " + errorString + ")");
	}
	*/
	abort();
}

//******************************************************************************************************************
bool Equi2Cube::convertImage(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{	
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(!inputPath.empty());
	GARDEN_ASSERT(!outputPath.empty());

	auto path = inputPath / filePath;
	vector<uint8> dataBuffer;

	if (!File::tryLoadBinary(path, dataBuffer))
		return false;

	auto extension = filePath.extension();
	uint8* equiPixels = nullptr; // Note: width * height * RGBA
	int sizeX = 0, sizeY = 0, binarySize = 0;

	if (extension == ".exr")
	{
		try
		{
			ExrMemoryStream exrStream(std::move(dataBuffer));
			Imf::RgbaInputFile exrFile(exrStream);
		
			auto dw = exrFile.dataWindow();
			sizeX = dw.max.x - dw.min.x + 1;
			sizeY = dw.max.y - dw.min.y + 1;
			binarySize = 8;

			equiPixels = malloc<uint8>((psize)sizeX * sizeY * binarySize);
			exrFile.setFrameBuffer((Imf::Rgba*)equiPixels - dw.min.x - dw.min.y * sizeX, 1, sizeX);
			exrFile.readPixels(dw.min.y, dw.max.y);
		}
		catch (exception& e)
		{
			free(equiPixels);
			throw GardenError("Failed to load EXR image. ("
				"path: " + filePath.generic_string() + ", error: " + string(e.what()) + ")");
		}
	}
	else if (extension == ".hdr")
	{
		equiPixels = (uint8*)stbi_loadf_from_memory(dataBuffer.data(),
			(int)dataBuffer.size(), &sizeX, &sizeY, nullptr, 4);
		if (!equiPixels)
			throw GardenError("Failed to load HDR image. (path: " + filePath.generic_string() + ")");
		binarySize = 16;
	}
	else
	{
		throw GardenError("Unsupported image file extension. ("
			"path: " + filePath.generic_string() + ")");
	}
	// TODO: also convert png/jpg cubemaps.

	auto equiSize = uint2((uint32)sizeX, (uint32)sizeY);
	auto cubemapSize = equiSize.x / 4;

	if (equiSize.x / 2 != equiSize.y || cubemapSize % 32 != 0)
	{
		free(equiPixels);
		throw GardenError("Image is not a cubemap. (path: " + filePath.generic_string() + ")");
	}

	auto invDim = 1.0f / cubemapSize;
	auto equiSizeMinus1 = equiSize - 1u;
	auto pixelsSize = (psize)cubemapSize * cubemapSize * binarySize;

	vector<uint8> left(pixelsSize), right(pixelsSize), bottom(pixelsSize),
		top(pixelsSize), back(pixelsSize), front(pixelsSize);

	if (binarySize == 8) // half
	{
		Imf::Rgba* cubeFaces[6] =
		{
			(Imf::Rgba*)left.data(), (Imf::Rgba*)right.data(), (Imf::Rgba*)bottom.data(), 
			(Imf::Rgba*)top.data(), (Imf::Rgba*)back.data(), (Imf::Rgba*)front.data(),
		};
		::convert(cubeFaces, cubemapSize, equiSize, equiSizeMinus1, (Imf::Rgba*)equiPixels, invDim);
	}
	else if (binarySize == 16) // float
	{
		f32x4* cubeFaces[6] =
		{
			(f32x4*)left.data(), (f32x4*)right.data(), (f32x4*)bottom.data(), 
			(f32x4*)top.data(), (f32x4*)back.data(), (f32x4*)front.data(),
		};
		convert(cubeFaces, cubemapSize, equiSize, equiSizeMinus1, (f32x4*)equiPixels, invDim);
	}
	else abort();

	constexpr auto imageFormat = Image::Format::SfloatR16G16B16A16;
	auto cacheFilePath = (outputPath / filePath).generic_string();
	cacheFilePath.resize(cacheFilePath.length() - 4);
	writeExrImageData(cacheFilePath + "-nx.exr", cubemapSize, left, imageFormat, true);
	writeExrImageData(cacheFilePath + "-px.exr", cubemapSize, right, imageFormat, true);
	writeExrImageData(cacheFilePath + "-ny.exr", cubemapSize, bottom, imageFormat, true);
	writeExrImageData(cacheFilePath + "-py.exr", cubemapSize, top, imageFormat, true);
	writeExrImageData(cacheFilePath + "-nz.exr", cubemapSize, back, imageFormat, true);
	writeExrImageData(cacheFilePath + "-pz.exr", cubemapSize, front, imageFormat, true);
	return true;
}
#endif

#ifdef EQUI2CUBE
//******************************************************************************************************************
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "equi2cube: error: no image file name" << endl;
		return EXIT_FAILURE;
	}

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
				"  --version     Display converter version information." << endl;
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "--version") == 0)
		{
			cout << "equi2cube " GARDEN_VERSION_STRING << endl;
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "-i") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no input directory" << endl;
				return EXIT_FAILURE;
			}

			inputPath = argv[i + 1]; i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no output directory" << endl;
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1]; i++;
		}
		else if (strcmp(arg, "-t") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "equi2cube: error: no thread count" << endl;
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
			i++;
		}
		else if (arg[0] == '-')
		{
			cout << string("equi2cube: error: unsupported option: '") + arg + "'" << endl;
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
					cout << string("equi2cube: error: no image file found (") + arg + ")\n" << flush;
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
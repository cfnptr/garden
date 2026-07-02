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

#include "garden/graphics/image-converter.hpp"
#include "garden/file.hpp"

#include <atomic>
#include <iostream>

using namespace garden;
using namespace garden::graphics;
using namespace math::ibl;

#if GARDEN_DEBUG || defined(GARDEN_IMAGE_CONVERTER)
//**********************************************************************************************************************
f32x4 ImageConverter::filterCubeMap(float2 coords, const f32x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
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
f16x4 ImageConverter::filterCubeMap(float2 coords, const f16x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
{
	auto coords0 = min((uint2)coords, sizeMinus1);
	auto coords1 = min(coords0 + uint2::one, sizeMinus1);
	auto uv = coords - coords0, invUV = 1.0f - uv;

	auto s0 = f32x4(pixels[coords0.y * sizeX + coords0.x]);
	auto s1 = f32x4(pixels[coords0.y * sizeX + coords1.x]);
	auto s2 = f32x4(pixels[coords1.y * sizeX + coords0.x]);
	auto s3 = f32x4(pixels[coords1.y * sizeX + coords1.x]);

	return f16x4(min(fma(s0, f32x4(invUV.x * invUV.y), fma(s1, f32x4(uv.x * invUV.y),
		fma(s2, f32x4(invUV.x * uv.y), s3 * (uv.x * uv.y)))), f32x4(FLOAT_BIG_16)));
}
Color ImageConverter::filterCubeMap(float2 coords, const Color* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
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

// TODO: Use instead math::Sampler, implement linear filtering inside it.

//******************************************************************************************************************
bool ImageConverter::compress(const fs::path& filePath, const fs::path& inputPath, const fs::path& outputPath)
{
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(!inputPath.empty());
	GARDEN_ASSERT(!outputPath.empty());

	auto path = inputPath / filePath; vector<uint8> imageData;
	if (!File::tryLoadBinary(path, imageData))
		return false;

	auto extension = filePath.extension();
	GARDEN_ASSERT(!extension.empty());

	vector<uint8> pixels; uint4 size; Image::Type type; Image::Format format; 
	Image::loadFileData(imageData.data(), imageData.size(), toImageFileType(
		extension.generic_string()), pixels, size, type, format);

	auto metadataPath = path; metadataPath.replace_extension(".meta");
	float effort = GARDEN_DEBUG ? 0.0f : 0.7f; auto storeFlags = Image::StoreFlag::None;
	if (size.w > 1) storeFlags |= Image::StoreFlag::GenerateMips;

	if (fs::exists(metadataPath))
		Image::loadFileMetadata(metadataPath, pixels, size, type, format, effort, storeFlags);
	fs::create_directories(outputPath);

	auto gicFilePath = (outputPath / filePath).replace_extension(".gic");
	Image::storeFileData(gicFilePath, pixels.data(), (uint3)size, 
		Image::FileType::GIC, type, format, 1.0f, effort, storeFlags);
	return true;
}

//******************************************************************************************************************
bool ImageConverter::equi2cube(const fs::path& filePath, const fs::path& inputPath, 
	const fs::path& outputPath, ThreadPool* threadPool)
{	
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(!inputPath.empty());
	GARDEN_ASSERT(!outputPath.empty());

	auto path = inputPath / filePath; vector<uint8> imageData;
	if (!File::tryLoadBinary(path, imageData))
		return false;

	auto extension = filePath.extension();
	GARDEN_ASSERT(!extension.empty());
	vector<uint8> equiPixels; uint4 equiSize; Image::Type imageType;
	auto imageFormat = Image::Format::Undefined;

	Image::loadFileData(imageData.data(), imageData.size(), toImageFileType(
		extension.generic_string()), equiPixels, equiSize, imageType, imageFormat);

	auto cubemapSize = equiSize.x / 4;
	if (equiSize.z != 1 || equiSize.x / 2 != equiSize.y || cubemapSize % 32 != 0)
		throw GardenError("Image is not a cubemap.");

	auto invDim = 1.0f / cubemapSize; auto equiSizeMinus1 = (uint2)equiSize - 1u;
	auto faceBinarySize = toBinarySize((psize)cubemapSize * cubemapSize, imageFormat);
	GARDEN_ASSERT_MSG(faceBinarySize > 0, "Assert " + filePath.generic_string());

	vector<uint8> nx(faceBinarySize), px(faceBinarySize), ny(faceBinarySize), 
		py(faceBinarySize), nz(faceBinarySize), pz(faceBinarySize);
	if (imageFormat == Image::Format::SfloatR16G16B16A16)
	{
		f16x4* cubeFaces[Image::cubemapFaceCount] =
		{
			(f16x4*)nx.data(), (f16x4*)px.data(), (f16x4*)ny.data(), 
			(f16x4*)py.data(), (f16x4*)nz.data(), (f16x4*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, (uint2)equiSize, equiSizeMinus1, (f16x4*)equiPixels.data(), invDim);
	}
	else if (imageFormat == Image::Format::SfloatR32G32B32A32)
	{
		f32x4* cubeFaces[Image::cubemapFaceCount] =
		{
			(f32x4*)nx.data(), (f32x4*)px.data(), (f32x4*)ny.data(), 
			(f32x4*)py.data(), (f32x4*)nz.data(), (f32x4*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, (uint2)equiSize, equiSizeMinus1, (f32x4*)equiPixels.data(), invDim);
	}
	else if (imageFormat == Image::Format::SrgbR8G8B8A8)
	{
		Color* cubeFaces[Image::cubemapFaceCount] =
		{
			(Color*)nx.data(), (Color*)px.data(), (Color*)ny.data(), 
			(Color*)py.data(), (Color*)nz.data(), (Color*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, (uint2)equiSize, equiSizeMinus1, (Color*)equiPixels.data(), invDim);
	}
	else throw GardenError("Unsupported equirectangular image data format.");
	equiPixels = {}; // Note: Cleaning up memory.

	auto imageSize = uint3(cubemapSize, cubemapSize, 1);
	constexpr auto fileType = Image::FileType::GIC;
	constexpr auto effort = GARDEN_DEBUG ? 0.0f : 0.7f;
	auto gicFilePath = (outputPath / filePath).replace_extension().generic_string();
	fs::create_directories(outputPath);

	if (threadPool)
	{
		threadPool->addTasks([&](const ThreadPool::Task& task)
		{
			switch (task.getTaskIndex())
			{
				case 0: Image::storeFileData(gicFilePath + "-nx.gic", nx.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				case 1: Image::storeFileData(gicFilePath + "-px.gic", px.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				case 2: Image::storeFileData(gicFilePath + "-ny.gic", ny.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				case 3: Image::storeFileData(gicFilePath + "-py.gic", py.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				case 4: Image::storeFileData(gicFilePath + "-nz.gic", nz.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				case 5: Image::storeFileData(gicFilePath + "-pz.gic", pz.data(), 
					imageSize, fileType, imageType, imageFormat, 1.0f, effort); break;
				default: abort();
			}
		}, Image::cubemapFaceCount);
		threadPool->wait();
	}
	else
	{
		Image::storeFileData(gicFilePath + "-nx.gic", nx.data(), 
			imageSize, fileType, imageType, imageFormat, 1.0f, effort);
		Image::storeFileData(gicFilePath + "-px.gic", px.data(), 
			imageSize, fileType, imageType, imageFormat, 1.0f, effort);
		Image::storeFileData(gicFilePath + "-ny.gic", ny.data(),
			 imageSize, fileType, imageType, imageFormat, 1.0f, effort);
		Image::storeFileData(gicFilePath + "-py.gic", py.data(), 
			imageSize, fileType, imageType, imageFormat, 1.0f, effort);
		Image::storeFileData(gicFilePath + "-nz.gic", nz.data(), 
			imageSize, fileType, imageType, imageFormat, 1.0f, effort);
		Image::storeFileData(gicFilePath + "-pz.gic", pz.data(), 
			imageSize, fileType, imageType, imageFormat, 1.0f, effort);
	}
	return true;
}
#endif

#ifdef GARDEN_IMAGE_CONVERTER
//******************************************************************************************************************
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "imagec: error: no image file name" << endl;
		return EXIT_FAILURE;
	}

	fs::path workingPath = fs::path(argv[0]).parent_path();
	auto inputPath = workingPath, outputPath = workingPath;
	ThreadPool* threadPool = nullptr; atomic_int convertResult = true;
	auto equi2cube = false;
	
	for (int i = 1; i < argc; i++)
	{
		auto arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
		{
			cout << "(C) 2022-" GARDEN_CURRENT_YEAR " Nikita Fediuchin. All rights reserved.\n"
				"imagec - Garden engine image converter.\n"
				"\n"
				"Usage: imagec [options] name...\n"
				"\n"
				"Options:\n"
				"  -i <dir>      Read input files from <dir>.\n"
				"  -o <dir>      Write output files to <dir>.\n"
				"  -t <value>    Specify thread pool size. (Uses all cores by default)\n"
				"  -e            Convert equirectangular images to cubemaps."
				"  -h            Display available imagec options.\n"
				"  --help        Display available imagec options.\n"
				"  --version     Display converter version information." << endl;
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "--version") == 0)
		{
			cout << "imagec " GARDEN_VERSION_STRING << endl;
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "-i") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "imagec: error: no input directory" << endl;
				return EXIT_FAILURE;
			}

			inputPath = argv[i + 1]; i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "imagec: error: no output directory" << endl;
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1]; i++;
		}
		else if (strcmp(arg, "-t") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "imagec: error: no thread count" << endl;
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
		else if (strcmp(arg, "-e") == 0)
		{
			equi2cube = true;
			i++;
		}
		else if (arg[0] == '-')
		{
			cout << string("imagec: error: unsupported option: '") + arg + "'" << endl;
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

				auto result = equi2cube ? ImageConverter::equi2cube(arg, inputPath, outputPath) :
					ImageConverter::compress(arg, inputPath, outputPath);
				if (!result)
					cout << string("imagec: error: no image file found (") + arg + ")\n" << flush;
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
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
#include "garden/file.hpp"

#include <atomic>
#include <iostream>

using namespace garden;
using namespace garden::graphics;
using namespace math::ibl;

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
f16x4 Equi2Cube::filterCubeMap(float2 coords, const f16x4* pixels, uint2 sizeMinus1, uint32 sizeX) noexcept
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

//******************************************************************************************************************
bool Equi2Cube::convertImage(const fs::path& filePath, const fs::path& inputPath, 
	const fs::path& outputPath, ThreadPool* threadPool)
{	
	GARDEN_ASSERT(!filePath.empty());
	GARDEN_ASSERT(!inputPath.empty());
	GARDEN_ASSERT(!outputPath.empty());

	auto path = inputPath / filePath; vector<uint8> dataBuffer;
	if (!File::tryLoadBinary(path, dataBuffer))
		return false;

	auto extension = filePath.extension();
	GARDEN_ASSERT(!extension.empty());
	auto imageFormat = Image::Format::Undefined;
	vector<uint8> equiPixels; uint2 equiSize = uint2::zero; 

	try
	{
		Image::loadFileData(dataBuffer.data(), dataBuffer.size(), equiPixels, equiSize, 
			toImageFileType(extension.generic_string().c_str() + 1), imageFormat);
	}
	catch (exception& e)
	{
		throw GardenError("Failed to load equi image data. (path: " + 
			filePath.generic_string() + ", error: " + string(e.what()) + ")");
	}

	auto cubemapSize = equiSize.x / 4;
	if (equiSize.x / 2 != equiSize.y || cubemapSize % 32 != 0)
		throw GardenError("Image is not a cubemap. (path: " + filePath.generic_string() + ")");

	auto invDim = 1.0f / cubemapSize; auto equiSizeMinus1 = equiSize - 1u;
	auto pixelsSize = (psize)cubemapSize * cubemapSize * toBinarySize(imageFormat);
	vector<uint8> nx(pixelsSize), px(pixelsSize), ny(pixelsSize), py(pixelsSize), nz(pixelsSize), pz(pixelsSize);

	if (imageFormat == Image::Format::SfloatR16G16B16A16)
	{
		f16x4* cubeFaces[6] =
		{
			(f16x4*)nx.data(), (f16x4*)px.data(), (f16x4*)ny.data(), 
			(f16x4*)py.data(), (f16x4*)nz.data(), (f16x4*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, equiSize, equiSizeMinus1, (f16x4*)equiPixels.data(), invDim);
	}
	else if (imageFormat == Image::Format::SfloatR32G32B32A32)
	{
		f32x4* cubeFaces[6] =
		{
			(f32x4*)nx.data(), (f32x4*)px.data(), (f32x4*)ny.data(), 
			(f32x4*)py.data(), (f32x4*)nz.data(), (f32x4*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, equiSize, equiSizeMinus1, (f32x4*)equiPixels.data(), invDim);
	}
	else if (imageFormat == Image::Format::SrgbR8G8B8A8)
	{
		Color* cubeFaces[6] =
		{
			(Color*)nx.data(), (Color*)px.data(), (Color*)ny.data(), 
			(Color*)py.data(), (Color*)nz.data(), (Color*)pz.data(),
		};
		convert(cubeFaces, cubemapSize, equiSize, equiSizeMinus1, (Color*)equiPixels.data(), invDim);
	}
	else throw GardenError("Unsupported equi image data format.");
	equiPixels = {}; // Cleaning up memory.

	auto imageSize = uint2(cubemapSize);
	static constexpr auto fileType = Image::FileType::EXR;
	auto exrFilePath = (outputPath / filePath).generic_string();
	exrFilePath.resize(exrFilePath.length() - 4);
	fs::create_directories(outputPath);

	if (threadPool)
	{
		threadPool->addTasks([&](const ThreadPool::Task& task)
		{
			switch (task.getTaskIndex())
			{
				case 0: Image::writeFileData(exrFilePath + "-nx.exr", nx.data(), imageSize, fileType, imageFormat); break;
				case 1: Image::writeFileData(exrFilePath + "-px.exr", px.data(), imageSize, fileType, imageFormat); break;
				case 2: Image::writeFileData(exrFilePath + "-ny.exr", ny.data(), imageSize, fileType, imageFormat); break;
				case 3: Image::writeFileData(exrFilePath + "-py.exr", py.data(), imageSize, fileType, imageFormat); break;
				case 4: Image::writeFileData(exrFilePath + "-nz.exr", nz.data(), imageSize, fileType, imageFormat); break;
				case 5: Image::writeFileData(exrFilePath + "-pz.exr", pz.data(), imageSize, fileType, imageFormat); break;
				default: abort();
			}
		}, Image::cubemapFaceCount);
		threadPool->wait();
	}
	else
	{
		Image::writeFileData(exrFilePath + "-nx.exr", nx.data(), imageSize, fileType, imageFormat);
		Image::writeFileData(exrFilePath + "-px.exr", px.data(), imageSize, fileType, imageFormat);
		Image::writeFileData(exrFilePath + "-ny.exr", ny.data(), imageSize, fileType, imageFormat);
		Image::writeFileData(exrFilePath + "-py.exr", py.data(), imageSize, fileType, imageFormat);
		Image::writeFileData(exrFilePath + "-nz.exr", nz.data(), imageSize, fileType, imageFormat);
		Image::writeFileData(exrFilePath + "-pz.exr", pz.data(), imageSize, fileType, imageFormat);
	}
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
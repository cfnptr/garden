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

#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/thread.hpp"
#include "garden/graphics/equi2cube.hpp"
#include "garden/graphics/compiler.hpp"
#include "garden/graphics/api.hpp"
#include "garden/json-serialize.hpp"
#include "garden/profiler.hpp"
#include "garden/file.hpp"

#include "math/ibl.hpp"
#include "math/tone-mapping.hpp"

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 0
#include "zlib.h"
#include "tinyexr.h"

#include "webp/decode.h"
#include "png.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <fstream>
#include <iostream>

using namespace garden;
using namespace math::ibl;

constexpr uint8 imageFileExtCount = 8;
constexpr string_view imageFileExts[] =
{
	".webp", ".png", ".jpg", ".jpeg", ".exr", ".hdr", ".bmp", ".psd", ".tga"
};
constexpr ImageFileType imageFileTypes[] =
{
	ImageFileType::Webp, ImageFileType::Png, ImageFileType::Jpg, ImageFileType::Jpg,
	ImageFileType::Exr, ImageFileType::Hdr, ImageFileType::Bmp, ImageFileType::Psd, ImageFileType::Tga
};

//**********************************************************************************************************************
namespace garden::graphics
{
	struct ImageLoadData final
	{
		uint64 version = 0;
		vector<fs::path> paths;
		ID<Image> instance = {};
		Image::Bind bind = {};
		Image::Strategy strategy = {};
		uint8 maxMipCount = 0;
		ImageLoadFlags flags = {};
	};
	struct PipelineLoadData
	{
		uint64 version = 0;
		fs::path shaderPath;
		map<string, Pipeline::SpecConstValue> specConstValues;
		map<string, Pipeline::SamplerState> samplerStateOverrides;
		uint32 maxBindlessCount = 0;
		#if !GARDEN_PACK_RESOURCES
		fs::path resourcesPath;
		fs::path cachesPath;
		#endif
	};
	struct GraphicsPipelineLoadData final : public PipelineLoadData
	{
		void* renderPass = nullptr;
		vector<Image::Format> colorFormats;
		map<uint8, GraphicsPipeline::State> pipelineStateOverrides;
		map<uint8, vector<GraphicsPipeline::BlendState>> blendStateOverrides;
		ID<GraphicsPipeline> instance = {};
		Image::Format depthStencilFormat = {};
		uint8 subpassIndex = 0;
		bool useAsyncRecording = false;
	};
	struct ComputePipelineLoadData final : public PipelineLoadData
	{
		ID<ComputePipeline> instance = {};
		bool useAsyncRecording = false;
	};

	/* TODO: refactor
	struct GeneralBufferLoadData final
	{
		uint64 version = 0;
		shared_ptr<Model> model;
		ID<Buffer> instance = {};
		ModelData::Accessor accessor;
		Buffer::Bind bind = {};
		Buffer::Access access = {};
		Buffer::Strategy strategy = {};

		GeneralBufferLoadData(ModelData::Accessor accessor) : accessor(accessor) { }
	};
	struct VertexBufferLoadData final
	{
		uint64 version = 0;
		vector<ModelData::Attribute::Type> attributes;
		shared_ptr<Model> model;
		ID<Buffer> instance = {};
		ModelData::Primitive primitive;
		Buffer::Bind bind = {};
		Buffer::Access access = {};
		Buffer::Strategy strategy = {};

		VertexBufferLoadData(ModelData::Primitive primitive) : primitive(primitive) { }
	};
	*/
}

//**********************************************************************************************************************
ResourceSystem::ResourceSystem(bool setSingleton) : Singleton(setSingleton)
{
	static_assert(std::numeric_limits<float>::is_iec559, "Floats are not IEEE 754");

	auto manager = Manager::Instance::get();
	manager->registerEvent("ImageLoaded");
	manager->registerEvent("BufferLoaded");

	ECSM_SUBSCRIBE_TO_EVENT("Init", ResourceSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ResourceSystem::deinit);

	#if GARDEN_PACK_RESOURCES
	try
	{
		packReader.open("resources.pack", true, thread::hardware_concurrency() + 1);
	}
	catch (const exception& e)
	{
		throw GardenError("Failed to open \"resources.pack\" file.");
	}
	#endif

	auto appInfoSystem = AppInfoSystem::Instance::get();
	appVersion = appInfoSystem->getVersion();

	#if GARDEN_DEBUG || GARDEN_EDITOR
	appResourcesPath = appInfoSystem->getResourcesPath();
	appCachesPath = appInfoSystem->getCachesPath();
	#endif
}
ResourceSystem::~ResourceSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ResourceSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ResourceSystem::deinit);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("ImageLoaded");
		manager->unregisterEvent("BufferLoaded");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void ResourceSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input);
}
void ResourceSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		while (loadedImageQueue.size() > 0)
		{
			auto& item = loadedImageQueue.front();
			BufferExt::destroy(item.staging);
			ImageExt::destroy(item.image);
			loadedImageQueue.pop();
		}
		while (loadedBufferQueue.size() > 0)
		{
			auto& item = loadedBufferQueue.front();
			BufferExt::destroy(item.staging);
			BufferExt::destroy(item.buffer);
			loadedBufferQueue.pop();
		}
		while (loadedComputeQueue.size() > 0)
		{
			PipelineExt::destroy(loadedComputeQueue.front().pipeline);
			loadedComputeQueue.pop();
		}
		while (loadedGraphicsQueue.size() > 0)
		{
			PipelineExt::destroy(loadedGraphicsQueue.front().pipeline);
			loadedGraphicsQueue.pop();
		}

		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", ResourceSystem::input);
	}
}

//**********************************************************************************************************************
void ResourceSystem::dequeuePipelines()
{
	SET_CPU_ZONE_SCOPED("Loaded Pipelines Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto graphicsPipelines = graphicsAPI->graphicsPipelinePool.getData();
	auto graphicsOccupancy = graphicsAPI->graphicsPipelinePool.getOccupancy();

	while (loadedGraphicsQueue.size() > 0)
	{
		auto& item = loadedGraphicsQueue.front();
		if (*item.instance <= graphicsOccupancy)
		{
			auto& pipeline = graphicsPipelines[*item.instance - 1];
			if (PipelineExt::getVersion(pipeline) == PipelineExt::getVersion(item.pipeline))
			{
				GraphicsPipelineExt::moveInternalObjects(item.pipeline, pipeline);
				GARDEN_LOG_TRACE("Loaded graphics pipeline. (path: " + pipeline.getPath().generic_string() + ")");
			}
			else
			{
				graphicsAPI->forceResourceDestroy = true;
				PipelineExt::destroy(item.pipeline);
				graphicsAPI->forceResourceDestroy = false;
			}
			
			if (item.renderPass)
			{
				auto& shareCount = graphicsAPI->renderPasses.at(item.renderPass);
				if (shareCount == 0)
				{
					graphicsAPI->destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer,
						nullptr, item.renderPass);
					graphicsAPI->renderPasses.erase(item.renderPass);
				}
				else
				{
					shareCount--;
				}
			}
		}
		loadedGraphicsQueue.pop();
	}

	auto computePipelines = graphicsAPI->computePipelinePool.getData();
	auto computeOccupancy = graphicsAPI->computePipelinePool.getOccupancy();

	while (loadedComputeQueue.size() > 0)
	{
		auto& item = loadedComputeQueue.front();
		if (*item.instance <= computeOccupancy)
		{
			auto& pipeline = computePipelines[*item.instance - 1];
			if (PipelineExt::getVersion(pipeline) == PipelineExt::getVersion(item.pipeline))
			{
				ComputePipelineExt::moveInternalObjects(item.pipeline, pipeline);
				GARDEN_LOG_TRACE("Loaded compute pipeline. (path: " + pipeline.getPath().generic_string() + ")");
			}
			else
			{
				graphicsAPI->forceResourceDestroy = true;
				PipelineExt::destroy(item.pipeline);
				graphicsAPI->forceResourceDestroy = false;
			}
		}
		loadedComputeQueue.pop();
	}
}

//**********************************************************************************************************************
void ResourceSystem::dequeueBuffersAndImages()
{
	SET_CPU_ZONE_SCOPED("Loaded Buffers/Images Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

	#if GARDEN_DEBUG
	if (!loadedBufferQueue.empty() || !loadedImageQueue.empty())
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		BEGIN_GPU_DEBUG_LABEL("Buffers Transfer", Color::transparent);
		graphicsSystem->stopRecording();
	}
	#endif

	while (loadedBufferQueue.size() > 0)
	{
		auto& item = loadedBufferQueue.front();
		if (*item.instance <= graphicsAPI->bufferPool.getOccupancy()) // getOccupancy() required, do not optimize!
		{
			auto& buffer = graphicsAPI->bufferPool.getData()[*item.instance - 1];
			if (MemoryExt::getVersion(buffer) == MemoryExt::getVersion(item.buffer))
			{
				BufferExt::moveInternalObjects(item.buffer, buffer);
				#if GARDEN_DEBUG || GARDEN_EDITOR
				buffer.setDebugName(buffer.getDebugName());
				#endif
			}
			else
			{
				graphicsAPI->forceResourceDestroy = true;
				BufferExt::destroy(item.buffer);
				graphicsAPI->forceResourceDestroy = false;
			}
			
			auto staging = graphicsAPI->bufferPool.create(Buffer::Bind::TransferSrc,
				Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
			SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loaded" + to_string(*staging));

			auto stagingView = graphicsAPI->bufferPool.get(staging);
			BufferExt::moveInternalObjects(item.staging, **stagingView);
			graphicsSystem->startRecording(CommandBufferType::TransferOnly);
			Buffer::copy(staging, item.instance);
			graphicsSystem->stopRecording();
			graphicsAPI->bufferPool.destroy(staging);

			loadedBuffer = item.instance;
			loadedBufferPath = std::move(item.path);
			manager->runEvent("BufferLoaded");
		}
		else
		{
			graphicsAPI->forceResourceDestroy = true;
			BufferExt::destroy(item.staging);
			graphicsAPI->forceResourceDestroy = false;
		}
		loadedBufferQueue.pop();
	}

	#if GARDEN_DEBUG
	if (!loadedBufferQueue.empty() || !loadedImageQueue.empty())
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		if (!loadedBufferQueue.empty())
		{
			END_GPU_DEBUG_LABEL();
		}
		if (!loadedImageQueue.empty())
		{
			BEGIN_GPU_DEBUG_LABEL("Images Transfer", Color::transparent);
		}
		graphicsSystem->stopRecording();
	}
	#endif

	auto images = graphicsAPI->imagePool.getData();
	auto imageOccupancy = graphicsAPI->imagePool.getOccupancy();

	while (loadedImageQueue.size() > 0)
	{
		auto& item = loadedImageQueue.front();
		if (*item.instance <= imageOccupancy)
		{
			auto& image = images[*item.instance - 1];
			if (MemoryExt::getVersion(image) == MemoryExt::getVersion(item.image))
			{
				ImageExt::moveInternalObjects(item.image, image);
				#if GARDEN_DEBUG || GARDEN_EDITOR
				image.setDebugName(image.getDebugName());
				#endif

				auto staging = graphicsAPI->bufferPool.create(Buffer::Bind::TransferSrc,
					Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
				SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loadedImage" + to_string(*staging));

				auto stagingView = graphicsAPI->bufferPool.get(staging);
				BufferExt::moveInternalObjects(item.staging, **stagingView);
				graphicsSystem->startRecording(CommandBufferType::TransferOnly);
				Image::copy(staging, item.instance);
				graphicsSystem->stopRecording();
				graphicsAPI->bufferPool.destroy(staging);

				loadedImage = item.instance;
				loadedImagePaths = std::move(item.paths);
				manager->runEvent("ImageLoaded");
			}
			else
			{
				graphicsAPI->forceResourceDestroy = true;
				ImageExt::destroy(item.image);
				graphicsAPI->forceResourceDestroy = false;
			}
		}
		else
		{
			graphicsAPI->forceResourceDestroy = true;
			BufferExt::destroy(item.staging);
			graphicsAPI->forceResourceDestroy = false;
		}
		loadedImageQueue.pop();
	}

	#if GARDEN_DEBUG
	if (!loadedImageQueue.empty())
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		END_GPU_DEBUG_LABEL();
		graphicsSystem->stopRecording();
	}
	#endif

	loadedBuffer = {};
	loadedImage = {};
	loadedImagePaths = {};
	loadedBufferPath = "";
}

//**********************************************************************************************************************
void ResourceSystem::input()
{
	SET_CPU_ZONE_SCOPED("Loaded Resources Update");

	auto manager = Manager::Instance::get();
	for (auto& buffer : loadedBufferArray)
	{
		loadedBuffer = buffer.instance;
		loadedBufferPath = std::move(buffer.path);
		manager->runEvent("BufferLoaded");
	}
	loadedBufferArray.clear();
	
	for (auto& image : loadedImageArray)
	{
		loadedImage = image.instance;
		loadedImagePaths = std::move(image.paths);
		manager->runEvent("ImageLoaded");
	}
	loadedImageArray.clear();

	queueLocker.lock();
	dequeuePipelines();
	dequeueBuffersAndImages();
	queueLocker.unlock();
}

//**********************************************************************************************************************
static void loadMissingImage(vector<uint8>& data, uint2& size, Image::Format& format)
{
	data.resize(sizeof(Color) * 16);
	auto pixels = (Color*)data.data();
	pixels[0] = Color::magenta; pixels[1] = Color::black;    pixels[2] = Color::magenta;  pixels[3] = Color::black;
	pixels[4] = Color::black;   pixels[5] = Color::magenta;  pixels[6] = Color::black;    pixels[7] = Color::magenta;
	pixels[8] = Color::magenta; pixels[9] = Color::black;    pixels[10] = Color::magenta; pixels[11] = Color::black;
	pixels[12] = Color::black;  pixels[13] = Color::magenta; pixels[14] = Color::black;   pixels[15] = Color::magenta;
	size = uint2(4, 4);
	format = Image::Format::SrgbR8G8B8A8;
}
static void loadMissingImageFloat(vector<uint8>& data, uint2& size, Image::Format& format)
{
	const f32x4 colorMagenta = (f32x4)Color::magenta; const f32x4 colorBlack = (f32x4)Color::black;
	data.resize(sizeof(f32x4) * 16);
	auto pixels = (f32x4*)data.data();
	pixels[0] = colorMagenta; pixels[1] = colorBlack;    pixels[2] = colorMagenta;  pixels[3] = colorBlack;
	pixels[4] = colorBlack;   pixels[5] = colorMagenta;  pixels[6] = colorBlack;    pixels[7] = colorMagenta;
	pixels[8] = colorMagenta; pixels[9] = colorBlack;    pixels[10] = colorMagenta; pixels[11] = colorBlack;
	pixels[12] = colorBlack;  pixels[13] = colorMagenta; pixels[14] = colorBlack;   pixels[15] = colorMagenta;
	size = uint2(4, 4);
	format = Image::Format::SfloatR32G32B32A32;
}
static void loadMissingImageFloat(vector<uint8>& left, vector<uint8>& right, vector<uint8>& bottom,
	vector<uint8>& top, vector<uint8>& back, vector<uint8>& front, uint2& size, Image::Format& format)
{
	loadMissingImageFloat(left, size, format);
	loadMissingImageFloat(right, size, format);
	loadMissingImageFloat(bottom, size, format);
	loadMissingImageFloat(top, size, format);
	loadMissingImageFloat(back, size, format);
	loadMissingImageFloat(front, size, format);
}

#if !GARDEN_PACK_RESOURCES
//**********************************************************************************************************************
static int32 getImageFilePath(const fs::path& appCachesPath, const fs::path& appResourcesPath,
	fs::path path, fs::path& filePath, ImageFileType& fileType)
{
	auto imagePath = fs::path("images") / path;
	filePath = appCachesPath / imagePath;

	filePath += ".exr";
	fileType = ImageFileType::Exr;
	int32 fileCount = fs::exists(filePath) ? 1 : 0;

	for (uint8 i = 0; i < imageFileExtCount; i++)
	{
		imagePath.replace_extension(imageFileExts[i]);
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileType = imageFileTypes[i];
			fileCount++;
		}
	}

	imagePath = fs::path("models") / path;
	for (uint8 i = 0; i < imageFileExtCount; i++)
	{
		imagePath.replace_extension(imageFileExts[i]);
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileType = imageFileTypes[i];
			fileCount++;
		}
	}

	return fileCount;
}
#endif

//**********************************************************************************************************************
void ResourceSystem::loadImageData(const fs::path& path, vector<uint8>& data,
	uint2& size, Image::Format& format, int32 threadIndex) const
{
	// TODO: store images as bc compressed for polygon geometry. KTX 2.0
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	ImageFileType fileType;
	vector<uint8> dataBuffer;

	#if GARDEN_PACK_RESOURCES
	if (threadIndex < 0)
		threadIndex = 0;
	else
		threadIndex++;

	auto imagePath = fs::path("images") / path;
	uint64 itemIndex = 0;

	for (uint8 i = 0; i < imageFileExtCount; i++)
	{
		imagePath.replace_extension(imageFileExts[i]);
		if (packReader.getItemIndex(imagePath, itemIndex))
			fileType = imageFileTypes[i];
	}

	if (fileType == ImageFileType::Count)
	{
		GARDEN_LOG_ERROR("Image does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(data, size, format);
		return;
	}

	packReader.readItemData(itemIndex, dataBuffer, threadIndex);
	#else
	fs::path filePath;
	auto fileCount = getImageFilePath(appCachesPath, appResourcesPath, path, filePath, fileType);

	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("Image file does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(data, size, format);
		return;
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("Image file is ambiguous. (path: " + path.generic_string() + ")");
		loadMissingImage(data, size, format);
		return;
	}

	File::loadBinary(filePath, dataBuffer);
	#endif

	loadImageData(dataBuffer.data(), dataBuffer.size(), fileType, data, size, format);
	GARDEN_LOG_TRACE("Loaded image. (path: " + path.generic_string() + ")");
}

#if !GARDEN_PACK_RESOURCES
//**********************************************************************************************************************
static void writeExrImageData(const fs::path& filePath, uint32 size, const vector<uint8>& data)
{
	auto directory = filePath.parent_path();
	if (!fs::exists(directory))
		fs::create_directories(directory);

	const char* error = nullptr;
	auto result = SaveEXR((const float*)data.data(), size, size,
		4, false, filePath.generic_string().c_str(), &error);

	if (result != TINYEXR_SUCCESS)
	{
		auto errorString = string(error);
		FreeEXRErrorMessage(error);
		throw GardenError("Failed to store EXR image. ("
			"path: " + filePath.generic_string() + ", error: " + errorString + ")");
	}
}
#endif

static void clampFloatImageData(vector<uint8>& equiData)
{
	auto pixelData = (f32x4*)equiData.data();
	auto pixelCount = equiData.size() / sizeof(f32x4);

	for (psize i = 0; i < pixelCount; i++)
	{
		auto pixel = pixelData[i];
		pixelData[i] = min(pixel, f32x4(65504.0f));
	}
}

//**********************************************************************************************************************
static void convertCubemapImageData(ThreadSystem* threadSystem, const vector<uint8>& equiData, 
	uint2 equiSize, vector<uint8>& left, vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top, 
	vector<uint8>& back, vector<uint8>& front, Image::Format format, int32 threadIndex)
{
	SET_CPU_ZONE_SCOPED("Cubemap Data Convert");

	vector<f32x4> floatData; const f32x4* equiPixels;
	if (format == Image::Format::SrgbR8G8B8A8)
	{
		floatData.resize(equiData.size() / sizeof(Color));
		auto dstData = floatData.data();
		auto srcData = (const Color*)equiData.data();

		if (threadIndex < 0 && threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems([&](const ThreadPool::Task& task)
			{
				auto itemCount = task.getItemCount();
				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
					dstData[i] = fastGammaCorrection((f32x4)srcData[i]);
			},
			(uint32)floatData.size());
			threadPool.wait();
		}
		else
		{
			for (uint32 i = 0; i < (uint32)floatData.size(); i++)
				dstData[i] = fastGammaCorrection((f32x4)srcData[i]);
		}

		equiPixels = floatData.data();
	}
	else
	{
		equiPixels = (f32x4*)equiData.data();
	}

	auto cubemapSize = equiSize.x / 4;
	auto invDim = 1.0f / cubemapSize;
	auto equiSizeMinus1 = equiSize - uint2::one;
	auto pixelsSize = sizeof(f32x4) * cubemapSize * cubemapSize;
	left.resize(pixelsSize); right.resize(pixelsSize);
	bottom.resize(pixelsSize); top.resize(pixelsSize);
	back.resize(pixelsSize); front.resize(pixelsSize);

	f32x4* cubePixelArray[6] =
	{
		(f32x4*)right.data(), (f32x4*)left.data(),
		(f32x4*)top.data(), (f32x4*)bottom.data(),
		(f32x4*)front.data(), (f32x4*)back.data(),
	};

	if (threadIndex < 0 && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([&](const ThreadPool::Task& task)
		{
			auto sizeXY = cubemapSize * cubemapSize;
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				uint3 coords;
				coords.z = i / sizeXY;
				coords.y = (i - coords.z * sizeXY) / cubemapSize;
				coords.x = i - (coords.y * cubemapSize + coords.z * sizeXY);
				auto cubePixels = cubePixelArray[coords.z];

				Equi2Cube::convert(coords, cubemapSize, equiSize,
					equiSizeMinus1, equiPixels, cubePixels, invDim);
			}
		},
		cubemapSize * cubemapSize * 6);
		threadPool.wait();
	}
	else
	{
		for (uint32 face = 0; face < 6; face++)
		{
			auto cubePixels = cubePixelArray[face];
			for (int32 y = 0; y < cubemapSize; y++)
			{
				for (int32 x = 0; x < cubemapSize; x++)
				{
					Equi2Cube::convert(uint3(x, y, face), cubemapSize,
						equiSize, equiSizeMinus1, equiPixels, cubePixels, invDim);
				}
			}
		}
	}
}

//**********************************************************************************************************************
void ResourceSystem::loadCubemapData(const fs::path& path, vector<uint8>& left,
	vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top, vector<uint8>& back, 
	vector<uint8>& front, uint2& size, bool clamp16, int32 threadIndex) const
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());
	auto threadSystem = ThreadSystem::Instance::tryGet();
	
	#if !GARDEN_PACK_RESOURCES
	auto filePath = appCachesPath / "images" / path;
	auto cacheFilePath = filePath.generic_string();

	fs::path inputFilePath; ImageFileType inputFileType; Image::Format format;
	auto fileCount = getImageFilePath(appCachesPath, appResourcesPath, path, inputFilePath, inputFileType);

	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("Cubemap image file does not exist. (path: " + path.generic_string() + ")");
		loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
		return;
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("Cubemap image file is ambiguous. (path: " + path.generic_string() + ")");
		loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
		return;
	}
	
	auto inputLastWriteTime = fs::last_write_time(inputFilePath);
	if (!fs::exists(cacheFilePath + "-nx.exr") || !fs::exists(cacheFilePath + "-px.exr") ||
		!fs::exists(cacheFilePath + "-ny.exr") || !fs::exists(cacheFilePath + "-py.exr") ||
		!fs::exists(cacheFilePath + "-nz.exr") || !fs::exists(cacheFilePath + "-pz.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-nx.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-px.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-ny.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-py.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-nz.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFilePath + "-pz.exr"))
	{
		vector<uint8> equiData; uint2 equiSize;
		loadImageData(path, equiData, equiSize, format, threadIndex);

		auto cubemapSize = equiSize.x / 4;
		if (equiSize.x / 2 != equiSize.y)
		{
			GARDEN_LOG_ERROR("Invalid equi cubemap size. (path: " + path.generic_string() + ")");
			loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
			return;
		}
		if (cubemapSize % 32 != 0)
		{
			GARDEN_LOG_ERROR("Invalid cubemap image size. (path: " + path.generic_string() + ")");
			loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
			return;
		}

		if (clamp16 && format == Image::Format::SfloatR32G32B32A32)
			clampFloatImageData(equiData);
		convertCubemapImageData(threadSystem, equiData, equiSize,
			left, right, bottom, top, back, front, format, threadIndex);
		size = uint2(equiSize.x / 4, equiSize.y / 2);

		fs::create_directories(filePath.parent_path());
		if (threadIndex < 0 && threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addTasks([&](const ThreadPool::Task& task)
			{
				switch (task.getTaskIndex())
				{
					case 0: writeExrImageData(cacheFilePath + "-nx.exr", cubemapSize, left); break;
					case 1: writeExrImageData(cacheFilePath + "-px.exr", cubemapSize, right); break;
					case 2: writeExrImageData(cacheFilePath + "-ny.exr", cubemapSize, bottom); break;
					case 3: writeExrImageData(cacheFilePath + "-py.exr", cubemapSize, top); break;
					case 4: writeExrImageData(cacheFilePath + "-nz.exr", cubemapSize, back); break;
					case 5: writeExrImageData(cacheFilePath + "-pz.exr", cubemapSize, front); break;
					default: abort();
				}
			}, 6);
			threadPool.wait();
		}
		else
		{
			writeExrImageData(cacheFilePath + "-nx.exr", cubemapSize, left);
			writeExrImageData(cacheFilePath + "-px.exr", cubemapSize, right);
			writeExrImageData(cacheFilePath + "-ny.exr", cubemapSize, bottom);
			writeExrImageData(cacheFilePath + "-py.exr", cubemapSize, top);
			writeExrImageData(cacheFilePath + "-nz.exr", cubemapSize, back);
			writeExrImageData(cacheFilePath + "-pz.exr", cubemapSize, front);
		}

		GARDEN_LOG_TRACE("Converted spherical cubemap. (path: " + path.generic_string() + ")");
		return;
	}
	#endif

	uint2 leftSize, rightSize, bottomSize, topSize, backSize, frontSize;
	Image::Format leftFormat, rightFormat, bottomFormat, topFormat, backFormat, frontFormat;

	if (threadIndex < 0 && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks([&](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Cubemap Data Load");

			auto filePath = path.generic_string();
			switch (task.getTaskIndex())
			{
			case 0: loadImageData(filePath + "-nx", left, leftSize, leftFormat, task.getThreadIndex()); break;
			case 1: loadImageData(filePath + "-px", right, rightSize, rightFormat, task.getThreadIndex()); break;
			case 2: loadImageData(filePath + "-ny", bottom, bottomSize, bottomFormat, task.getThreadIndex()); break;
			case 3: loadImageData(filePath + "-py", top, topSize, topFormat, task.getThreadIndex()); break;
			case 4: loadImageData(filePath + "-nz", back, backSize, backFormat, task.getThreadIndex()); break;
			case 5: loadImageData(filePath + "-pz", front, frontSize, frontFormat, task.getThreadIndex()); break;
			default: abort();
			}
		}, 6);
		threadPool.wait();
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Cubemap Data Load");

		auto filePath = path.generic_string();
		loadImageData(filePath + "-nx", left, leftSize, leftFormat, threadIndex);
		loadImageData(filePath + "-px", right, rightSize, rightFormat, threadIndex);
		loadImageData(filePath + "-ny", bottom, bottomSize, bottomFormat, threadIndex);
		loadImageData(filePath + "-py", top, topSize, topFormat, threadIndex);
		loadImageData(filePath + "-nz", back, backSize, backFormat, threadIndex);
		loadImageData(filePath + "-pz", front, frontSize, frontFormat, threadIndex);
	}

	#if GARDEN_DEBUG
	if (leftSize != rightSize || leftSize != bottomSize ||
		leftSize != topSize || leftSize != backSize || leftSize != frontSize ||
		leftSize.x % 32 != 0 || rightSize.x % 32 != 0 || bottomSize.x % 32 != 0 ||
		topSize.x % 32 != 0 || backSize.x % 32 != 0 || frontSize.x % 32 != 0)
	{
		GARDEN_LOG_ERROR("Invalid cubemap size. (path: " + path.generic_string() + ")");
		loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
		return;
	}
	if (leftSize.x != leftSize.y || rightSize.x != rightSize.y ||
		bottomSize.x != bottomSize.y || topSize.x != topSize.y ||
		backSize.x != backSize.y || frontSize.x != frontSize.y)
	{
		GARDEN_LOG_ERROR("Invalid cubemap side size. (path: " + path.generic_string() + ")");
		loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
		return;
	}
	if (leftFormat != rightFormat || leftFormat != bottomFormat ||
		leftFormat != topFormat || leftFormat != backFormat || leftFormat != frontFormat)
	{
		GARDEN_LOG_ERROR("Invalid cubemap format. (path: " + path.generic_string() + ")");
		loadMissingImageFloat(left, right, bottom, top, back, front, size, format);
		return;
	}
	#endif

	size = leftSize;
}

//**********************************************************************************************************************
void ResourceSystem::loadImageData(const uint8* data, psize dataSize, ImageFileType fileType,
	vector<uint8>& pixels, uint2& imageSize, Image::Format& format)
{
	if (fileType == ImageFileType::Webp)
	{
		int sizeX = 0, sizeY = 0;
		if (!WebPGetInfo(data, dataSize, &sizeX, &sizeY))
			throw GardenError("Invalid WebP image info.");
		imageSize = uint2(sizeX, sizeY);
		pixels.resize(sizeof(Color) * imageSize.x * imageSize.y);
		auto decodeResult = WebPDecodeRGBAInto(data, dataSize,
			pixels.data(), pixels.size(), (int)(imageSize.x * sizeof(Color)));
		if (!decodeResult)
			throw GardenError("Invalid WebP image data.");
	}
	else if (fileType == ImageFileType::Png)
	{
		png_image image;
		memset(&image, 0, (sizeof image));
		image.version = PNG_IMAGE_VERSION;

		if (!png_image_begin_read_from_memory(&image, data, dataSize))
			throw GardenError("Invalid PNG image info.");

		image.format = PNG_FORMAT_RGBA;
		imageSize = uint2(image.width, image.height);
		pixels.resize(PNG_IMAGE_SIZE(image));

		if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
			throw GardenError("Invalid PNG image data.");
	}
	else if (fileType == ImageFileType::Jpg || fileType == ImageFileType::Bmp ||
		fileType == ImageFileType::Psd || fileType == ImageFileType::Tga)
	{
		int sizeX = 0, sizeY = 0;
		auto pixelData = stbi_load_from_memory(data,
			(int)dataSize, &sizeX, &sizeY, nullptr, 4);
		if (!pixelData)
			throw GardenError("Invalid JPG image data.");
		imageSize = uint2(sizeX, sizeY);
		pixels.resize(sizeof(Color) * imageSize.x * imageSize.y);
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else if (fileType == ImageFileType::Exr)
	{
		int sizeX = 0, sizeY = 0;
		float* pixelData = nullptr;
		const char* error = nullptr;

		auto result = LoadEXRFromMemory(&pixelData, &sizeX, &sizeY, data, dataSize, &error);
		if (result != TINYEXR_SUCCESS)
		{
			auto errorString = string(error);
			FreeEXRErrorMessage(error);
			throw GardenError(errorString);
		}

		imageSize = uint2(sizeX, sizeY);
		pixels.resize(sizeof(f32x4) * imageSize.x * imageSize.y);
		memcpy(pixels.data(), pixelData, pixels.size());
		free(pixelData);
	}
	else if (fileType == ImageFileType::Hdr)
	{
		int sizeX = 0, sizeY = 0;
		auto pixelData = stbi_loadf_from_memory(data,
			(int)dataSize, &sizeX, &sizeY, nullptr, 4);
		if (!pixelData)
			throw GardenError("Invalid HDR image data.");
		imageSize = uint2(sizeX, sizeY);
		pixels.resize(sizeof(f32x4) * imageSize.x * imageSize.y);
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else abort();

	switch (fileType)
	{
	case garden::ImageFileType::Exr:
	case garden::ImageFileType::Hdr:
		format = Image::Format::SfloatR32G32B32A32;
		break;
	default:
		format = Image::Format::SrgbR8G8B8A8;
		break;
	}
}

//**********************************************************************************************************************
static void loadImageArrayData(ResourceSystem* resourceSystem, const vector<fs::path>& paths,
	vector<vector<uint8>>& pixelArrays, uint2& size, Image::Format& format, int32 threadIndex)
{
	resourceSystem->loadImageData(paths[0], pixelArrays[0], size, format, threadIndex);

	for (uint32 i = 1; i < (uint32)paths.size(); i++)
	{
		uint2 elementSize; Image::Format elementFormat;
		resourceSystem->loadImageData(paths[i], pixelArrays[i], elementSize, elementFormat, threadIndex);

		if (size != elementSize || format != elementFormat)
		{
			auto count = size.x * size.y;
			pixelArrays[i].resize(toBinarySize(format) * count);

			if (format == Image::Format::SfloatR32G32B32A32)
			{
				auto pixels = (f32x4*)pixelArrays[i].data();
				for (uint32 j = 0; j < count; j++)
					pixels[j] = f32x4(1.0f, 0.0f, 1.0f, 1.0f); // TODO: or maybe use checkerboard pattern?
			}
			else
			{
				auto pixels = (Color*)pixelArrays[i].data();
				for (uint32 j = 0; j < count; j++)
					pixels[j] = Color::magenta;
			}
		}
	}
}
static void copyLoadedImageData(const vector<vector<uint8>>& pixelArrays, uint8* stagingMap,
	uint2 realSize, uint2 imageSize, psize formatBinarySize, ImageLoadFlags flags) noexcept
{
	psize mapOffset = 0;
	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray) && realSize.x > realSize.y)
	{
		auto pixels = pixelArrays[0].data();
		auto layerCount = realSize.x / realSize.y;
		auto lineSize = formatBinarySize * imageSize.x;
		uint32 offsetX = 0;

		for (uint32 l = 0; l < layerCount; l++)
		{
			for (uint32 y = 0; y < imageSize.y; y++)
			{
				auto pixelsOffset = (psize)(y * realSize.x + offsetX) * formatBinarySize;
				memcpy(stagingMap + mapOffset, pixels + pixelsOffset, lineSize);
				mapOffset += lineSize;
			}
			offsetX += imageSize.x;
		}
	}
	else
	{
		for (auto& pixels : pixelArrays)
		{
			memcpy(stagingMap + mapOffset, pixels.data(), pixels.size());
			mapOffset += pixels.size();
		}
	}
}

//**********************************************************************************************************************
static void calcLoadedImageDim(psize pathCount, uint2 realSize,
	ImageLoadFlags flags, uint2& imageSize, uint32& layerCount) noexcept
{
	imageSize = realSize;
	layerCount = (uint32)pathCount;

	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray))
	{
		if (realSize.x > realSize.y)
		{
			layerCount = realSize.x / realSize.y;
			imageSize.x = imageSize.y;
		}
		else
		{
			layerCount = realSize.y / realSize.x;
			imageSize.y = imageSize.x;
		}

		if (layerCount == 0) layerCount = 1;
	}
}
static Image::Type calcLoadedImageType(psize pathCount, uint32 sizeY, ImageLoadFlags flags) noexcept
{
	if (pathCount > 1)
	{
		if (hasAnyFlag(flags, ImageLoadFlags::CubemapType))
			return Image::Type::Cubemap;
		if (hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::ArrayType))
			return sizeY == 1 ? Image::Type::Texture1DArray : Image::Type::Texture2DArray;
	}
	return sizeY == 1 ? Image::Type::Texture1D : Image::Type::Texture2D;
}
static uint8 calcLoadedImageMipCount(uint8 maxMipCount, uint2 imageSize) noexcept
{
	return maxMipCount == 0 ? calcMipCount(imageSize) : std::min(maxMipCount, calcMipCount(imageSize));
}

//**********************************************************************************************************************
Ref<Image> ResourceSystem::loadImageArray(const vector<fs::path>& paths, Image::Bind bind,
	uint8 maxMipCount, Image::Strategy strategy, ImageLoadFlags flags)
{
	// TODO: allow to load file with image paths to load image arrays.
	#if GARDEN_DEBUG
	GARDEN_ASSERT(!paths.empty());
	GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));

	if (paths.size() > 1)
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::LoadArray));
	}
	else
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::CubemapType));
	}

	string debugName = hasAnyFlag(flags, ImageLoadFlags::LoadShared) ? "shared." : "";
	#endif

	Hash128 hash;
	if (hasAnyFlag(flags, ImageLoadFlags::LoadShared))
	{
		auto hashState = Hash128::getState();
		Hash128::resetState(hashState);
		for (const auto& item : paths)
		{
			auto path = item.generic_string();
			Hash128::updateState(hashState, path.c_str(), path.length());
		}

		Hash128::updateState(hashState, &bind, sizeof(Image::Bind));
		Hash128::updateState(hashState, &maxMipCount, sizeof(uint8));
		Hash128::updateState(hashState, &flags, sizeof(ImageLoadFlags));
		hash = Hash128::digestState(hashState);

		auto result = sharedImages.find(hash);
		if (result != sharedImages.end())
		{
			auto imageView = GraphicsSystem::Instance::get()->get(result->second);
			if (imageView->isLoaded())
			{
				LoadedImageItem item;
				item.paths = paths;
				item.instance = ID<Image>(result->second);
				loadedImageArray.push_back(std::move(item));
			}
			return result->second;
		}
	}
	
	auto graphicsAPI = GraphicsAPI::get();
	auto version = graphicsAPI->imageVersion++;
	auto image = graphicsAPI->imagePool.create(bind, strategy, version);

	#if GARDEN_DEBUG
	auto resource = graphicsAPI->imagePool.get(image);
	if (paths.size() > 1 || hasAnyFlag(flags, ImageLoadFlags::LoadArray))
		resource->setDebugName("imageArray." + debugName + paths[0].generic_string());
	else
		resource->setDebugName("image." + debugName + paths[0].generic_string());
	#endif

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (!hasAnyFlag(flags, ImageLoadFlags::LoadSync) && threadSystem)
	{
		auto data = new ImageLoadData();
		data->version = version;
		data->paths = paths;
		data->instance = image;
		data->bind = bind;
		data->strategy = strategy;
		data->maxMipCount = maxMipCount;
		data->flags = flags;

		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Image Array Load");

			auto& paths = data->paths;
			vector<vector<uint8>> pixelArrays(paths.size()); uint2 realSize; Image::Format format;
			loadImageArrayData(this, paths, pixelArrays, realSize, format, task.getThreadIndex());
			
			if (hasAnyFlag(data->flags, ImageLoadFlags::LinearData) && format == Image::Format::SrgbR8G8B8A8)
				format = Image::Format::UnormR8G8B8A8;

			auto formatBinarySize = toBinarySize(format);
			uint2 imageSize; uint32 layerCount;
			calcLoadedImageDim(paths.size(), realSize, data->flags, imageSize, layerCount);
			auto type = calcLoadedImageType(paths.size(), realSize.y, data->flags);
			auto mipCount = calcLoadedImageMipCount(data->maxMipCount, imageSize);

			ImageQueueItem item =
			{
				ImageExt::create(type, format, data->bind, data->strategy, 
					u32x4(imageSize.x, imageSize.y, 1), mipCount, layerCount, data->version),
				BufferExt::create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite, Buffer::Usage::Auto,
					Buffer::Strategy::Speed, formatBinarySize * realSize.x * realSize.y, 0),
				std::move(paths),
				realSize,
				data->instance,
			};

			copyLoadedImageData(pixelArrays, item.staging.getMap(),
				realSize, imageSize, formatBinarySize, data->flags);
			item.staging.flush();

			delete data;
			queueLocker.lock();
			loadedImageQueue.push(std::move(item));
			queueLocker.unlock();
		});
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Image Array Load");

		vector<vector<uint8>> pixelArrays(paths.size()); uint2 realSize; Image::Format format;
		loadImageArrayData(this, paths, pixelArrays, realSize, format, -1);

		if (hasAnyFlag(flags, ImageLoadFlags::LinearData) && format == Image::Format::SrgbR8G8B8A8)
			format = Image::Format::UnormR8G8B8A8;

		auto formatBinarySize = toBinarySize(format);
		uint2 imageSize; uint32 layerCount;
		calcLoadedImageDim(paths.size(), realSize, flags, imageSize, layerCount);
		auto type = calcLoadedImageType(paths.size(), realSize.y, flags);
		auto mipCount = calcLoadedImageMipCount(maxMipCount, imageSize);

		auto imageInstance = ImageExt::create(type, format, bind, strategy,
			u32x4(imageSize.x, imageSize.y, 1), mipCount, layerCount, 0);
		auto imageView = graphicsAPI->imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto staging = graphicsAPI->bufferPool.create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, formatBinarySize * realSize.x * realSize.y, 0);
		SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loadedImage" + to_string(*staging));
		auto stagingView = graphicsAPI->bufferPool.get(staging);

		copyLoadedImageData(pixelArrays, stagingView->getMap(),
			realSize, imageSize, formatBinarySize, flags);
		stagingView->flush();

		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		Image::copy(staging, image);
		graphicsSystem->stopRecording();
		graphicsAPI->bufferPool.destroy(staging);

		LoadedImageItem item;
		item.paths = paths;
		item.instance = image;
		loadedImageArray.push_back(std::move(item));
	}

	auto imageRef = Ref<Image>(image);
	if (hasAnyFlag(flags, ImageLoadFlags::LoadShared))
	{
		auto result = sharedImages.emplace(hash, imageRef);
		GARDEN_ASSERT(result.second); // Corrupted shared images array.
	}

	return imageRef;
}

void ResourceSystem::destroyShared(const Ref<Image>& image)
{
	if (!image || image.getRefCount() > 2)
		return;

	for (auto i = sharedImages.begin(); i != sharedImages.end(); i++)
	{
		if (i->second != image)
			continue;
		sharedImages.erase(i);
		break;
	}

	if (image.isLastRef())
		GraphicsSystem::Instance::get()->destroy(ID<Image>(image));
}

//**********************************************************************************************************************
void ResourceSystem::destroyShared(const Ref<Buffer>& buffer)
{
	if (!buffer || buffer.getRefCount() > 2)
		return;

	for (auto i = sharedBuffers.begin(); i != sharedBuffers.end(); i++)
	{
		if (i->second != buffer)
			continue;
		sharedBuffers.erase(i);
		break;
	}

	if (buffer.isLastRef())
		GraphicsSystem::Instance::get()->destroy(ID<Buffer>(buffer));
}

//**********************************************************************************************************************
Ref<DescriptorSet> ResourceSystem::createSharedDS(const Hash128& hash, ID<GraphicsPipeline> graphicsPipeline, 
	map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchResult = sharedDescriptorSets.find(hash);
	if (searchResult != sharedDescriptorSets.end())
		return searchResult->second;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto descriptorSet = graphicsSystem->createDescriptorSet(graphicsPipeline, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT(result.second); // Corrupted shared descriptor sets array.
	return sharedDescriptorSet;
}
Ref<DescriptorSet> ResourceSystem::createSharedDS(const Hash128& hash, ID<ComputePipeline> computePipeline, 
	map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(computePipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchResult = sharedDescriptorSets.find(hash);
	if (searchResult != sharedDescriptorSets.end())
		return searchResult->second;

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto descriptorSet = graphicsSystem->createDescriptorSet(computePipeline, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT(result.second); // Corrupted shared descriptor sets array.
	return sharedDescriptorSet;
}

void ResourceSystem::destroyShared(const Ref<DescriptorSet>& descriptorSet)
{
	if (!descriptorSet || descriptorSet.getRefCount() > 2)
		return;

	for (auto i = sharedDescriptorSets.begin(); i != sharedDescriptorSets.end(); i++)
	{
		if (i->second != descriptorSet)
			continue;
		sharedDescriptorSets.erase(i);
		break;
	}

	if (descriptorSet.isLastRef())
		GraphicsSystem::Instance::get()->destroy(ID<DescriptorSet>(descriptorSet));
}

//**********************************************************************************************************************
static bool loadOrCompileGraphics(Compiler::GraphicsData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto vertexPath = "shaders" / data.shaderPath; vertexPath += ".vert";
	auto fragmentPath = "shaders" / data.shaderPath; fragmentPath += ".frag";
	auto headerPath = "shaders" / data.shaderPath; headerPath += ".gslh";

	fs::path vertexInputPath, fragmentInputPath;
	auto hasVertexShader = File::tryGetResourcePath(data.resourcesPath, vertexPath, vertexInputPath);
	auto hasFragmentShader = File::tryGetResourcePath(data.resourcesPath, fragmentPath, fragmentInputPath);

	if (!hasVertexShader && !hasFragmentShader)
	{
		throw GardenError("Graphics shader file does not exist or it is ambiguous. ("
			"path: " + data.shaderPath.generic_string() + ")");
	}

	vertexPath += ".spv"; fragmentPath += ".spv";
	auto vertexOutputPath = data.cachesPath / vertexPath;
	auto fragmentOutputPath = data.cachesPath / fragmentPath;
	auto headerFilePath = data.cachesPath / headerPath;
	
	if (!fs::exists(headerFilePath) ||
		(hasVertexShader && (!fs::exists(vertexOutputPath) ||
		fs::last_write_time(vertexInputPath) > fs::last_write_time(vertexOutputPath))) ||
		(hasFragmentShader && (!fs::exists(fragmentOutputPath) ||
		fs::last_write_time(fragmentInputPath) > fs::last_write_time(fragmentOutputPath))))
	{
		const vector<fs::path> includePaths =
		{
			GARDEN_RESOURCES_PATH / "shaders",
			data.resourcesPath / "shaders"
		};

		fs::path inputPath, outputPath;
		if (hasVertexShader)
		{
			inputPath = vertexInputPath.parent_path();
			outputPath = vertexOutputPath.parent_path();
		}
		else
		{
			inputPath = fragmentInputPath.parent_path();
			outputPath = fragmentOutputPath.parent_path();
		}

		auto compileResult = false;
		try
		{
			auto dataPath = data.shaderPath; data.shaderPath = dataPath.filename();
			compileResult = Compiler::compileGraphicsShaders(inputPath, outputPath, includePaths, data);
			data.shaderPath = dataPath;
		}
		catch (const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << vertexInputPath.generic_string() << "(.frag):" << e.what() << "\n"; // TODO: get info which stage throw.
			GARDEN_LOG_ERROR("Failed to compile graphics shaders. (name: " + data.shaderPath.generic_string() + ")");
			return false;
		}
		
		if (!compileResult)
			throw GardenError("Shader files does not exist. (path: " + data.shaderPath.generic_string() + ")");
		GARDEN_LOG_TRACE("Compiled graphics shaders. (path: " + data.shaderPath.generic_string() + ")");
		return true;
	}
	#endif

	try
	{
		Compiler::loadGraphicsShaders(data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load graphics shaders. ("
			"name: " + data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> ResourceSystem::loadGraphicsPipeline(const fs::path& path,
	ID<Framebuffer> framebuffer,  bool useAsyncRecording, bool loadAsync, uint8 subpassIndex,
	uint32 maxBindlessCount, const map<string, Pipeline::SpecConstValue>& specConstValues,
	const GraphicsPipeline::StateOverrides* stateOverrides)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(framebuffer);

	// TODO: validate specConstValues and stateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto framebufferView = graphicsAPI->framebufferPool.get(framebuffer);
	const auto& subpasses = framebufferView->getSubpasses();

	GARDEN_ASSERT((subpasses.empty() && subpassIndex == 0) ||
		(!subpasses.empty() && subpassIndex < subpasses.size()));

	auto version = graphicsAPI->graphicsPipelineVersion++;
	auto pipeline = graphicsAPI->graphicsPipelinePool.create(path,
		maxBindlessCount, useAsyncRecording, version, framebuffer, subpassIndex);

	void* renderPass = nullptr;
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		renderPass = FramebufferExt::getRenderPass(**framebufferView);
		if (renderPass)
			graphicsAPI->renderPasses.at(renderPass)++;
	}
	else abort();

	vector<Image::Format> colorFormats;
	if (subpasses.empty())
	{
		const auto& colorAttachments = framebufferView->getColorAttachments();
		colorFormats.resize(colorAttachments.size());
		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			if (!colorAttachments[i].imageView)
			{
				colorFormats[i] = Image::Format::Undefined;
				continue;
			}
			
			auto attachment = graphicsAPI->imageViewPool.get(colorAttachments[i].imageView);
			colorFormats[i] = attachment->getFormat();
		}
	}
	else
	{
		const auto& outputAttachments = subpasses[subpassIndex].outputAttachments;
		if (!outputAttachments.empty())
		{
			auto attachment = graphicsAPI->imageViewPool.get(
				outputAttachments[outputAttachments.size() - 1].imageView);
			colorFormats.resize(isFormatColor(attachment->getFormat()) ?
				(uint32)outputAttachments.size() : (uint32)outputAttachments.size() - 1);
		}
	}

	auto depthStencilFormat = Image::Format::Undefined;
	if (framebufferView->getDepthStencilAttachment().imageView)
	{
		auto attachment = graphicsAPI->imageViewPool.get(
			framebufferView->getDepthStencilAttachment().imageView);
		depthStencilFormat = attachment->getFormat();
	}

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (loadAsync && threadSystem)
	{
		auto data = new GraphicsPipelineLoadData();
		data->shaderPath = path;
		data->version = version;
		data->renderPass = renderPass;
		data->subpassIndex = subpassIndex;
		data->colorFormats = std::move(colorFormats);
		data->specConstValues = specConstValues;
		data->instance = pipeline;
		data->maxBindlessCount = maxBindlessCount;
		data->depthStencilFormat = depthStencilFormat;
		data->subpassIndex = subpassIndex;
		data->useAsyncRecording = useAsyncRecording;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachesPath = appCachesPath;
		#endif

		if (stateOverrides)
		{
			data->samplerStateOverrides = stateOverrides->samplerStates;
			data->pipelineStateOverrides = stateOverrides->pipelineStates;
			data->blendStateOverrides = stateOverrides->blendStates;
		}
		
		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Graphics Pipeline Load");

			Compiler::GraphicsData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			pipelineData.colorFormats = std::move(data->colorFormats);
			pipelineData.pipelineStateOverrides = std::move(data->pipelineStateOverrides);
			pipelineData.blendStateOverrides = std::move(data->blendStateOverrides);
			pipelineData.renderPass = data->renderPass;
			pipelineData.subpassIndex = data->subpassIndex;
			pipelineData.depthStencilFormat = data->depthStencilFormat;
			#if GARDEN_PACK_RESOURCES
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#else
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachesPath = std::move(data->cachesPath);
			#endif

			if (!loadOrCompileGraphics(pipelineData))
			{
				delete data;
				return;
			}

			GraphicsQueueItem item =
			{
				GraphicsPipelineExt::create(pipelineData, data->useAsyncRecording),
				data->renderPass,
				data->instance
			};

			delete data;
			queueLocker.lock();
			loadedGraphicsQueue.push(std::move(item));
			queueLocker.unlock();
		},
		pipelineTaskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Graphics Pipeline Load");

		vector<uint8> vertexCode, fragmentCode;
		Compiler::GraphicsData pipelineData;
		pipelineData.shaderPath = path;
		pipelineData.specConstValues = specConstValues;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;
		pipelineData.colorFormats = std::move(colorFormats);
		pipelineData.renderPass = renderPass;
		pipelineData.subpassIndex = subpassIndex;
		pipelineData.depthStencilFormat = depthStencilFormat;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachesPath = appCachesPath;
		#endif

		if (stateOverrides)
		{
			pipelineData.samplerStateOverrides = stateOverrides->samplerStates;
			pipelineData.pipelineStateOverrides = stateOverrides->pipelineStates;
			pipelineData.blendStateOverrides = stateOverrides->blendStates;
		}

		if (!loadOrCompileGraphics(pipelineData))
		{
			throw GardenError("Failed to load graphics pipeline. ("
				"path: " + path.generic_string() + ")");
		}
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = graphicsAPI->graphicsPipelinePool.get(pipeline);
		GraphicsPipelineExt::moveInternalObjects(graphicsPipeline, **pipelineView);
		GARDEN_LOG_TRACE("Loaded graphics pipeline. (path: " +  path.generic_string() + ")");
	}

	return pipeline;
}

//**********************************************************************************************************************
static bool loadOrCompileCompute(Compiler::ComputeData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto computePath = "shaders" / data.shaderPath; computePath += ".comp";

	fs::path computeInputPath;
	if (!File::tryGetResourcePath(data.resourcesPath, computePath, computeInputPath))
		throw GardenError("Compute shader file does not exist, or it is ambiguous.");

	auto headerPath = "shaders" / data.shaderPath;
	computePath += ".spv"; headerPath += ".gslh";
	auto computeOutputPath = data.cachesPath / computePath;
	auto headerFilePath = data.cachesPath / headerPath;
	
	if (!fs::exists(headerFilePath) || !fs::exists(computeOutputPath) ||
		fs::last_write_time(computeInputPath) > fs::last_write_time(computeOutputPath))
	{
		const vector<fs::path> includePaths =
		{
			GARDEN_RESOURCES_PATH / "shaders",
			data.resourcesPath / "shaders"
		};

		auto compileResult = false;
		try
		{
			auto dataPath = data.shaderPath; data.shaderPath = dataPath.filename();
			compileResult = Compiler::compileComputeShader(computeInputPath.parent_path(),
				computeOutputPath.parent_path(), includePaths, data);
			data.shaderPath = dataPath;
		}
		catch (const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << computeInputPath.generic_string() << ":" << e.what() << "\n";
			GARDEN_LOG_ERROR("Failed to compile compute shader. (name: " + data.shaderPath.generic_string() + ")");
			return false;
		}
		
		if (!compileResult)
			throw GardenError("Shader file does not exist. (path: " + data.shaderPath.generic_string() + ")");
		GARDEN_LOG_TRACE("Compiled compute shader. (path: " + data.shaderPath.generic_string() + ")");
		return true;
	}
	#endif

	try
	{
		Compiler::loadComputeShader(data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load compute shader. ("
			"name: " + data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<ComputePipeline> ResourceSystem::loadComputePipeline(const fs::path& path,
	bool useAsyncRecording, bool loadAsync, uint32 maxBindlessCount,
	const map<string, Pipeline::SpecConstValue>& specConstValues,
	const map<string, Pipeline::SamplerState>& samplerStateOverrides)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto version = graphicsAPI->computePipelineVersion++;
	auto pipeline = graphicsAPI->computePipelinePool.create(path, maxBindlessCount, useAsyncRecording, version);

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (loadAsync && threadSystem)
	{
		auto data = new ComputePipelineLoadData();
		data->version = version;
		data->shaderPath = path;
		data->specConstValues = specConstValues;
		data->samplerStateOverrides = samplerStateOverrides;
		data->maxBindlessCount = maxBindlessCount;
		data->instance = pipeline;
		data->useAsyncRecording = useAsyncRecording;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachesPath = appCachesPath;
		#endif

		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Compute Pipeline Load");

			Compiler::ComputeData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			#if GARDEN_PACK_RESOURCES
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#else
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachesPath = std::move(data->cachesPath);
			#endif
			
			if (!loadOrCompileCompute(pipelineData))
			{
				delete data;
				return;
			}

			ComputeQueueItem item = 
			{
				ComputePipelineExt::create(pipelineData, data->useAsyncRecording),
				data->instance
			};

			delete data;
			queueLocker.lock();
			loadedComputeQueue.push(std::move(item));
			queueLocker.unlock();
		},
		pipelineTaskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Compute Pipeline Load");

		Compiler::ComputeData pipelineData;
		pipelineData.shaderPath = path;
		pipelineData.specConstValues = specConstValues;
		pipelineData.samplerStateOverrides = samplerStateOverrides;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachesPath = appCachesPath;
		#endif
		
		if (!loadOrCompileCompute(pipelineData))
		{
			throw GardenError("Failed to load compute pipeline. ("
				"path: " + path.generic_string() + ")");
		}

		auto computePipeline = ComputePipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = graphicsAPI->computePipelinePool.get(pipeline);
		ComputePipelineExt::moveInternalObjects(computePipeline, **pipelineView);
		GARDEN_LOG_TRACE("Loaded compute pipeline. (path: " + path.generic_string() + ")");
	}

	return pipeline;
}

//**********************************************************************************************************************
ID<Entity> ResourceSystem::loadScene(const fs::path& path, bool addRootEntity)
{
	GARDEN_ASSERT(!path.empty());
	JsonDeserializer deserializer;

	#if GARDEN_PACK_RESOURCES
	abort(); // TODO: load binary bson file from the resources, also handle case when scene does not exist
	#else
	fs::path filePath = "scenes" / path; filePath += ".scene"; fs::path scenePath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, scenePath))
	{
		GARDEN_LOG_ERROR("Scene file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(scenePath);
	}
	catch (exception& e)
	{
		GARDEN_LOG_TRACE("Failed to deserialize scene. ("
			"path: " + path.generic_string() + ", error: " + string(e.what()) + ")");
		return {};
	}
	#endif

	auto manager = Manager::Instance::get();
	TransformSystem* transformSystem = nullptr;
	ID<Entity> rootEntity = {};

	if (addRootEntity)
	{
		transformSystem = TransformSystem::Instance::tryGet();
		if (transformSystem)
		{
			rootEntity = manager->createEntity();
			auto transformView = manager->add<TransformComponent>(rootEntity);
			#if GARDEN_DEBUG || GARDEN_EDITOR
			transformView->debugName = path.generic_string();
			#endif
		}
		else
		{
			addRootEntity = false;
		}
	}

	const auto& systems = manager->getSystems();
	for (const auto& pair : systems)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializableSystem)
			continue;
		serializableSystem->preDeserialize(deserializer);
	}

	if (deserializer.beginChild("entities"))
	{
		const auto& componentNames = manager->getComponentNames();
		auto entityCount = (uint32)deserializer.getArraySize();

		string type;
		for (uint32 i = 0; i < entityCount; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			if (deserializer.beginChild("components"))
			{
				auto componentCount = (uint32)deserializer.getArraySize();
				if (componentCount == 0)
				{
					deserializer.endChild();
					continue;
				}

				auto entity = manager->createEntity();

				for (uint32 j = 0; j < componentCount; j++)
				{
					if (!deserializer.beginArrayElement(j))
						break;

					if (!deserializer.read(".type", type))
					{
						deserializer.endArrayElement();
						continue;
					}

					auto result = componentNames.find(type);
					if (result == componentNames.end())
					{
						deserializer.endArrayElement();
						continue;
					}

					auto system = result->second;
					auto serializableSystem = dynamic_cast<ISerializable*>(system);
					if (!serializableSystem)
					{
						deserializer.endArrayElement();
						continue;
					}

					auto componentView = manager->add(entity, system->getComponentType());
					serializableSystem->deserialize(deserializer, entity, componentView);
					deserializer.endArrayElement();
				}

				if (manager->getComponentCount(entity) == 0)
				{
					manager->destroy(entity);
				}
				else
				{
					if (addRootEntity)
					{
						auto transformView = transformSystem->tryGetComponent(entity);
						if (transformView)
							transformView->setParent(rootEntity);
					}
				}

				deserializer.endChild();
			}

			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	for (const auto& pair : systems)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializableSystem)
			continue;
		serializableSystem->postDeserialize(deserializer);
	}

	if (addRootEntity)
	{
		// Reducing root component memory consumption after serialization completion.
		auto transformView = manager->tryGet<TransformComponent>(rootEntity);
		if (transformView)
			transformView->shrinkChilds();
	}

	GARDEN_LOG_TRACE("Loaded scene. (path: " + path.generic_string() + ")");
	return rootEntity;
}

void ResourceSystem::clearScene()
{
	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::tryGet();
	const auto& entities = manager->getEntities();

	for (const auto& entity : entities)
	{
		auto entityID = entities.getID(&entity);
		if (entity.getComponents().empty() || manager->has<DoNotDestroyComponent>(entityID))
			continue;

		if (transformSystem)
		{
			auto transformView = transformSystem->tryGetComponent(entityID);
			if (transformView)
				transformView->setParent({});
		}
		manager->destroy(entityID);
	}

	GARDEN_LOG_TRACE("Cleaned scene.");
}

//**********************************************************************************************************************
void ResourceSystem::storeScene(const fs::path& path, ID<Entity> rootEntity, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());

	#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
	auto scenesPath = (directory.empty() ? appResourcesPath : directory) / "scenes";
	#else
	auto scenesPath = directory / "scenes";
	#endif

	auto directoryPath = scenesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = scenesPath / path; filePath += ".scene";
	JsonSerializer serializer(filePath);

	auto manager = Manager::Instance::get();
	auto transformSystem = TransformSystem::Instance::tryGet();
	if (rootEntity && (!transformSystem || !manager->has<TransformComponent>(rootEntity)))
		rootEntity = {};

	const auto& systems = manager->getSystems();
	for (const auto& pair : systems)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializableSystem)
			continue;
		serializableSystem->preSerialize(serializer);
	}

	serializer.write("version", appVersion.toString3());
	serializer.beginChild("entities");
	
	const auto& entities = manager->getEntities();
	auto dnsSystem = DoNotSerializeSystem::Instance::tryGet();

	for (const auto& entity : entities)
	{
		const auto& components = entity.getComponents();
		if (components.empty())
			continue;

		auto instance = entities.getID(&entity);

		View<TransformComponent> transformView = {};
		if (transformSystem)
			transformView = transformSystem->tryGetComponent(instance);

		if (rootEntity)
		{
			if (!transformView || (instance != rootEntity && !transformView->hasAncestor(rootEntity)))
				continue;
		}
		else
		{
			if (dnsSystem && dnsSystem->hasOrAncestors(instance))
				continue;
		}

		serializer.beginArrayElement();
		serializer.beginChild("components");

		for (const auto& pair : components)
		{
			auto system = pair.second.first;
			if (pair.first == typeid(DoNotDestroyComponent) || pair.first == typeid(DoNotDuplicateComponent))
			{
				serializer.beginArrayElement();
				serializer.write(".type", system->getComponentName());
				serializer.endArrayElement();
				continue;
			}

			auto serializableSystem = dynamic_cast<ISerializable*>(system);
			const auto& componentName = system->getComponentName();
			if (!serializableSystem || componentName.empty())
				continue;
			
			serializer.beginArrayElement();
			serializer.write(".type", componentName);
			auto componentView = system->getComponent(pair.second.second);
			serializableSystem->serialize(serializer, componentView);
			serializer.endArrayElement();
		}

		serializer.endChild();
		serializer.endArrayElement();
	}

	serializer.endChild();

	for (const auto& pair : systems)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializableSystem)
			continue;
		serializableSystem->postSerialize(serializer);
	}

	GARDEN_LOG_TRACE("Stored scene. (path: " + path.generic_string() + ")");
}

//**********************************************************************************************************************
Ref<Animation> ResourceSystem::loadAnimation(const fs::path& path, bool loadShared)
{
	GARDEN_ASSERT(!path.empty());
	JsonDeserializer deserializer;

	Hash128 hash;
	if (loadShared)
	{
		auto pathString = path.generic_string();
		auto hashState = Hash128::getState();
		Hash128::resetState(hashState);
		Hash128::updateState(hashState, pathString.c_str(), pathString.length());
		hash = Hash128::digestState(hashState);

		auto result = sharedAnimations.find(hash);
		if (result != sharedAnimations.end())
			return result->second;
	}

	#if GARDEN_PACK_RESOURCES
	abort(); // TODO: load binary bson file from the resources, also handle case when scene does not exist
	#else
	fs::path filePath = "animations" / path; filePath += ".anim"; fs::path animationPath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, animationPath))
	{
		GARDEN_LOG_ERROR("Animation file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(animationPath);
	}
	catch (exception& e)
	{
		GARDEN_LOG_TRACE("Failed to deserialize scene. ("
			"path: " + path.generic_string() + ", error: " + string(e.what()) + ")");
		return {};
	}
	#endif

	auto animationSystem = AnimationSystem::Instance::get();
	auto animation = animationSystem->createAnimation();
	auto animationView = animationSystem->get(animation);

	deserializer.read("frameRate", animationView->frameRate);
	deserializer.read("isLooped", animationView->isLooped);

	if (deserializer.beginChild("keyframes"))
	{
		const auto& componentNames = Manager::Instance::get()->getComponentNames();
		auto keyframeCount = (uint32)deserializer.getArraySize();

		for (uint32 i = 0; i < keyframeCount; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			int32 frame = 0;
			if (deserializer.read("frame", frame) && deserializer.beginChild("components"))
			{
				Animatables animatables;
				auto componentCount = (uint32)deserializer.getArraySize();
				for (uint32 j = 0; j < componentCount; j++)
				{
					if (!deserializer.beginArrayElement(j))
						break;

					string type;
					if (!deserializer.read(".type", type))
					{
						deserializer.endArrayElement();
						continue;
					}

					auto result = componentNames.find(type);
					if (result == componentNames.end())
					{
						deserializer.endArrayElement();
						continue;
					}

					auto system = result->second;
					auto animatableSystem = dynamic_cast<IAnimatable*>(system);
					if (!animatableSystem)
					{
						deserializer.endArrayElement();
						continue;
					}

					auto animationFrame = animatableSystem->deserializeAnimation(deserializer);
					if (!animationFrame)
					{
						deserializer.endArrayElement();
						continue;
					}

					auto animationFrameView = animatableSystem->getAnimation(animationFrame);
					deserializer.read(".coeff", animationFrameView->coeff);

					string funcType;
					if (deserializer.read(".funcType", funcType))
					{
						if (funcType == "Pow")
							animationFrameView->funcType = AnimationFunc::Pow;
						else if (funcType == "Gain")
							animationFrameView->funcType = AnimationFunc::Gain;
					}

					animatables.emplace(result->second, animationFrame);
					deserializer.endArrayElement();
				}

				if (!animatables.empty())
					animationView->emplaceKeyframe(frame, std::move(animatables));
				deserializer.endChild();
			}
			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	auto animationRef = Ref<Animation>(animation);
	if (loadShared)
	{
		auto result = sharedAnimations.emplace(hash, animationRef);
		GARDEN_ASSERT(result.second); // Corrupted shared animations array.
	}

	GARDEN_LOG_TRACE("Loaded animation. (path: " + path.generic_string() + ")");
	return animationRef;
}

void ResourceSystem::destroyShared(const Ref<Animation>& animation)
{
	if (!animation || animation.getRefCount() > 2)
		return;

	for (auto i = sharedAnimations.begin(); i != sharedAnimations.end(); i++)
	{
		if (i->second != animation)
			continue;
		sharedAnimations.erase(i);
		break;
	}

	if (animation.isLastRef())
		AnimationSystem::Instance::get()->destroy(ID<Animation>(animation));
}

//**********************************************************************************************************************
void ResourceSystem::storeAnimation(const fs::path& path, ID<Animation> animation, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(animation);

	#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
	auto animationsPath = (directory.empty() ? appResourcesPath : directory) / "animations";
	#else
	auto animationsPath = directory / "animations";
	#endif

	auto directoryPath = animationsPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = animationsPath / path; filePath += ".anim";
	JsonSerializer serializer(filePath);

	const auto animationView = AnimationSystem::Instance::get()->get(animation);
	serializer.write("frameRate", animationView->frameRate);
	serializer.write("isLooped", animationView->isLooped);

	const auto& keyframes = animationView->getKeyframes();
	if (keyframes.empty())
		return;

	serializer.beginChild("keyframes");
	for (const auto& keyframe : keyframes)
	{
		serializer.beginArrayElement();
		serializer.write("frame", keyframe.first);

		const auto& animatables = keyframe.second;
		if (animatables.empty())
		{
			serializer.endArrayElement();
			continue;
		}

		serializer.beginChild("components");
		for (const auto& animatable : animatables)
		{
			auto system = animatable.first;
			const auto& componentName = system->getComponentName();
			if (componentName.empty())
				continue;

			serializer.beginArrayElement();
			serializer.write(".type", componentName);

			auto animatableSystem = dynamic_cast<IAnimatable*>(system);
			auto frameView = animatableSystem->getAnimation(animatable.second);
			if (frameView->funcType == AnimationFunc::Pow)
				serializer.write(".funcType", string_view("Pow"));
			else if (frameView->funcType == AnimationFunc::Gain)
				serializer.write(".funcType", string_view("Gain"));
			serializer.write(".coeff", frameView->coeff);
			
			animatableSystem->serializeAnimation(serializer, frameView);
			serializer.endArrayElement();
		}
		serializer.endChild();

		serializer.endArrayElement();
	}
	serializer.endChild();

	GARDEN_LOG_TRACE("Stored animation. (path: " + path.generic_string() + ")");
}

/* TODO:
	for (int i = 0; i < count; i++)
	{
		auto path = fs::path(paths[i]).generic_string(); 
		auto length = path.length();
		if (length < 8)
			continue;

		psize pathOffset = 0;
		auto cmpPath = (GARDEN_RESOURCES_PATH / "scenes").generic_string();
		if (length > cmpPath.length())
		{
			if (memcmp(path.c_str(), cmpPath.c_str(), cmpPath.length()) == 0)
				pathOffset = cmpPath.length() + 1;
		}
		cmpPath = (GARDEN_APP_RESOURCES_PATH / "scenes").generic_string();
		if (length > cmpPath.length())
		{
			if (memcmp(path.c_str(), cmpPath.c_str(), cmpPath.length()) == 0)
				pathOffset = cmpPath.length() + 1;
		}

		if (memcmp(path.c_str() + (length - 5), "scene", 5) == 0)
		{
			fs::path filePath = path.c_str() + pathOffset; filePath.replace_extension();
			try
			{
				ResourceSystem::getInstance()->loadScene(filePath);
			}
			catch (const exception& e)
			{
				GARDEN_LOG_ERROR("Failed to load scene. (error: " + string(e.what()) + ")");
			}
			break;
		}
	}
*/

/* TODO: refactor
Ref<Buffer> ResourceSystem::loadBuffer(shared_ptr<Model> model, Model::Accessor accessor,
	Buffer::Bind bind, Buffer::Access access, Buffer::Strategy strategy, bool loadAsync)
{
	GARDEN_ASSERT(model);
	GARDEN_ASSERT(hasAnyFlag(bind, Buffer::Bind::TransferDst));

	auto version = GraphicsAPI::bufferVersion++;
	auto buffer = GraphicsAPI::bufferPool.create(bind,
		access, Buffer::Usage::PreferGPU, strategy, version);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.loaded" + to_string(*buffer)); // TODO: use model path for this buffer

	if (loadAsync && threadSystem)
	{
		auto data = new GeneralBufferLoadData(accessor);
		data->version = version;
		data->model = model;
		data->instance = buffer;
		data->bind = bind;
		data->access = access;
		data->strategy = strategy;

		threadSystem->getBackgroundPool().addTask([this](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Buffer Load");

			auto data = (GeneralBufferLoadData*)task.getArgument();
			loadModelBuffers(data->model);

			auto accessor = data->accessor;
			auto size = (uint64)(accessor.getCount() * accessor.getBinaryStride());

			BufferQueueItem item =
			{
				BufferExt::create(data->bind, data->access, Buffer::Usage::PreferGPU,
					data->strategy, size, data->version),
				BufferExt::create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
					Buffer::Usage::Auto, Buffer::Strategy::Speed, size, 0),
				data->instance,
			};

			accessor.copy(item.staging.getMap()); // TODO: convert uint8 to uintXX.
			item.staging.flush();

			delete data;
			queueLocker.lock();
			bufferQueue.push(std::move(item));
			queueLocker.unlock();
		},
		data);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Buffer Load");

		auto size = (uint64)(accessor.getCount() * accessor.getBinaryStride());
		auto bufferInstance = BufferExt::create(bind,
			access, Buffer::Usage::Auto, strategy, size, 0);
		auto bufferView = GraphicsAPI::bufferPool.get(buffer);
		BufferExt::moveInternalObjects(bufferInstance, **bufferView);

		auto staging = GraphicsAPI::bufferPool.create(
			Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, size, 0);
		SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loaded" + to_string(*staging));
		auto stagingView = GraphicsAPI::bufferPool.get(staging);
		accessor.copy(stagingView->getMap()); // TODO: convert uint8 to uintXX.
		stagingView->flush();
		graphicsSystem->startRecording(CommandBufferType::Frame);
		Buffer::copy(staging, buffer);
		graphicsSystem->stopRecording();
		GraphicsAPI::bufferPool.destroy(staging);
	}

	return buffer;
}

Ref<Buffer> ResourceSystem::loadVertexBuffer(shared_ptr<Model> model, Model::Primitive primitive,
	Buffer::Bind bind, const vector<Model::Attribute::Type>& attributes,
	Buffer::Access access, Buffer::Strategy strategy, bool loadAsync)
{
	GARDEN_ASSERT(model);
	GARDEN_ASSERT(hasAnyFlag(bind, Buffer::Bind::TransferDst));

	#if GARDEN_DEBUG
	auto hasAnyAttribute = false;
	for (auto attribute : attributes)
		hasAnyAttribute |= primitive.getAttributeIndex(attribute) >= 0;
	GARDEN_ASSERT(hasAnyAttribute);
	#endif

	auto version = GraphicsAPI::bufferVersion++;
	auto buffer = GraphicsAPI::bufferPool.create(bind,
		access, Buffer::Usage::PreferGPU, strategy, version);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.vertex.loaded" + to_string(*buffer)); // TODO: use model path

	if (loadAsync && threadSystem)
	{
		auto data = new VertexBufferLoadData(primitive);
		data->version = version;
		data->attributes = attributes;
		data->model = model;
		data->instance = buffer;
		data->bind = bind;
		data->access = access;
		data->strategy = strategy;

		threadSystem->getBackgroundPool().addTask([this](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Vertex Buffer Load");

			auto data = (VertexBufferLoadData*)task.getArgument();
			loadModelBuffers(data->model);

			auto primitive = data->primitive;
			auto& attributes = data->attributes;

			auto size = primitive.getVertexCount(attributes) *
				primitive.getBinaryStride(attributes);

			BufferQueueItem item =
			{
				BufferExt::create(data->bind, data->access, Buffer::Usage::PreferGPU,
					data->strategy, size, data->version),
				BufferExt::create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
					Buffer::Usage::Auto, data->strategy, size, 0),
				data->instance,
			};

			primitive.copyVertices(attributes, item.staging.getMap());
			item.staging.flush();

			delete data;
			queueLocker.lock();
			bufferQueue.push(std::move(item));
			queueLocker.unlock();
		},
		data);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Vertex Buffer Load");

		auto size = primitive.getVertexCount(attributes) *
			primitive.getBinaryStride(attributes);
		auto bufferInstance = BufferExt::create(bind,
			access, Buffer::Usage::Auto, strategy, size, 0);
		auto bufferView = GraphicsAPI::bufferPool.get(buffer);
		BufferExt::moveInternalObjects(bufferInstance, **bufferView);

		auto staging = GraphicsAPI::bufferPool.create(
			Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, size, 0);
		SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.vertexLoaded" + to_string(*staging));
		auto stagingView = GraphicsAPI::bufferPool.get(staging);
		primitive.copyVertices(attributes, stagingView->getMap());
		stagingView->flush();
		graphicsSystem->startRecording(CommandBufferType::Frame);
		Buffer::copy(staging, buffer);
		graphicsSystem->stopRecording();
		GraphicsAPI::bufferPool.destroy(staging);
	}

	return buffer;
}
*/
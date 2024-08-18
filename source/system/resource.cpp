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

#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/thread.hpp"
#include "garden/graphics/equi2cube.hpp"
#include "garden/graphics/compiler.hpp"
#include "garden/graphics/api.hpp"
#include "garden/json-serialize.hpp"
#include "garden/file.hpp"
#include "math/ibl.hpp"

#include "ImfIO.h"
#include "ImfHeader.h"
#include "ImfInputFile.h"
#include "ImfFrameBuffer.h"
#include "webp/decode.h"
#include "png.h"
#include "stb_image.h"

#include <fstream>

#if GARDEN_DEBUG
#include "ImfRgbaFile.h"
#include "webp/encode.h"
#endif

using namespace garden;
using namespace math::ibl;

static const uint8 imageFileExtCount = 9;
static const char* imageFileExts[] =
{
	".webp", ".png", ".exr", ".jpg", ".jpeg", ".hdr", ".bmp", ".psd", ".tga"
};
static const ImageFileType imageFileTypes[] =
{
	ImageFileType::Webp, ImageFileType::Png, ImageFileType::Exr, ImageFileType::Jpg, ImageFileType::Jpg,
	ImageFileType::Hdr, ImageFileType::Bmp, ImageFileType::Psd, ImageFileType::Tga
};

//**********************************************************************************************************************
namespace
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
		map<string, GraphicsPipeline::SamplerState> samplerStateOverrides;
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
		map<uint8, GraphicsPipeline::State> stateOverrides;
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

		GeneralBufferLoadData(ModelData::Accessor _accessor) : accessor(_accessor) { }
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

		VertexBufferLoadData(ModelData::Primitive _primitive) : primitive(_primitive) { }
	};
	*/
}

//**********************************************************************************************************************
ResourceSystem* ResourceSystem::instance = nullptr;

ResourceSystem::ResourceSystem()
{
	static_assert(std::numeric_limits<float>::is_iec559, "Floats are not IEEE 754");
	hashState = Hash128::createState();

	auto manager = Manager::get();
	manager->registerEvent("ImageLoaded");
	manager->registerEvent("BufferLoaded");

	SUBSCRIBE_TO_EVENT("Init", ResourceSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", ResourceSystem::deinit);

	#if GARDEN_PACK_RESOURCES
	packReader.open("resources.pack", true, thread::hardware_concurrency() + 1);
	#endif
	#if GARDEN_EDITOR
	auto appInfoSystem = AppInfoSystem::get();
	appResourcesPath = appInfoSystem->getResourcesPath();
	appCachesPath = appInfoSystem->getCachesPath();
	appVersion = appInfoSystem->getVersion();
	#endif

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
ResourceSystem::~ResourceSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", ResourceSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", ResourceSystem::deinit);

		auto manager = Manager::get();
		manager->unregisterEvent("ImageLoaded");
		manager->unregisterEvent("BufferLoaded");
	}

	Hash128::destroyState(hashState);

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
void ResourceSystem::init()
{
	SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input);
}
void ResourceSystem::deinit()
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

	if (Manager::get()->isRunning())
		UNSUBSCRIBE_FROM_EVENT("Input", ResourceSystem::input);
}

//**********************************************************************************************************************
void ResourceSystem::dequeuePipelines()
{
	#if GARDEN_DEBUG
	auto hasNewResources = !loadedGraphicsQueue.empty() || !loadedComputeQueue.empty();
	LogSystem* logSystem = nullptr;
	if (hasNewResources)
		logSystem = Manager::get()->tryGet<LogSystem>();
	#endif

	auto graphicsPipelines = GraphicsAPI::graphicsPipelinePool.getData();
	auto graphicsOccupancy = GraphicsAPI::graphicsPipelinePool.getOccupancy();

	while (loadedGraphicsQueue.size() > 0)
	{
		auto& item = loadedGraphicsQueue.front();
		if (*item.instance <= graphicsOccupancy)
		{
			auto& pipeline = graphicsPipelines[*item.instance - 1];
			if (PipelineExt::getVersion(pipeline) == PipelineExt::getVersion(item.pipeline))
			{
				GraphicsPipelineExt::moveInternalObjects(item.pipeline, pipeline);
				#if GARDEN_DEBUG
				if (logSystem)
					logSystem->trace("Loaded graphics pipeline. (path: " + pipeline.getPath().generic_string() + ")");
				#endif
			}
			else
			{
				GraphicsAPI::isRunning = false;
				PipelineExt::destroy(item.pipeline);
				GraphicsAPI::isRunning = true;
			}
			
			if (item.renderPass)
			{
				auto& shareCount = GraphicsAPI::renderPasses.at(item.renderPass);
				if (shareCount == 0)
				{
					GraphicsAPI::destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer,
						nullptr, item.renderPass);
					GraphicsAPI::renderPasses.erase(item.renderPass);
				}
				else
				{
					shareCount--;
				}
			}
		}
		loadedGraphicsQueue.pop();
	}

	auto computePipelines = GraphicsAPI::computePipelinePool.getData();
	auto computeOccupancy = GraphicsAPI::computePipelinePool.getOccupancy();

	while (loadedComputeQueue.size() > 0)
	{
		auto& item = loadedComputeQueue.front();
		if (*item.instance <= computeOccupancy)
		{
			auto& pipeline = computePipelines[*item.instance - 1];
			if (PipelineExt::getVersion(pipeline) == PipelineExt::getVersion(item.pipeline))
			{
				ComputePipelineExt::moveInternalObjects(item.pipeline, pipeline);
				#if GARDEN_DEBUG
				if (logSystem)
					logSystem->trace("Loaded compute pipeline. (path: " + pipeline.getPath().generic_string() + ")");
				#endif
			}
			else
			{
				GraphicsAPI::isRunning = false;
				PipelineExt::destroy(item.pipeline);
				GraphicsAPI::isRunning = true;
			}
		}
		loadedComputeQueue.pop();
	}
}

//**********************************************************************************************************************
void ResourceSystem::dequeueBuffersAndImages()
{
	auto manager = Manager::get();
	auto graphicsSystem = GraphicsSystem::get();

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
		if (*item.instance <= GraphicsAPI::bufferPool.getOccupancy()) // getOccupancy() required, do not optimize!
		{
			auto& buffer = GraphicsAPI::bufferPool.getData()[*item.instance - 1];
			if (MemoryExt::getVersion(buffer) == MemoryExt::getVersion(item.buffer))
			{
				BufferExt::moveInternalObjects(item.buffer, buffer);
				#if GARDEN_DEBUG || GARDEN_EDITOR
				buffer.setDebugName(buffer.getDebugName());
				#endif
			}
			else
			{
				GraphicsAPI::isRunning = false;
				BufferExt::destroy(item.buffer);
				GraphicsAPI::isRunning = true;
			}
			
			auto staging = GraphicsAPI::bufferPool.create(Buffer::Bind::TransferSrc,
				Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
			SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loaded" + to_string(*staging));

			auto stagingView = GraphicsAPI::bufferPool.get(staging);
			BufferExt::moveInternalObjects(item.staging, **stagingView);
			graphicsSystem->startRecording(CommandBufferType::TransferOnly);
			Buffer::copy(staging, item.instance);
			graphicsSystem->stopRecording();
			GraphicsAPI::bufferPool.destroy(staging);

			loadedBuffer = item.instance;
			loadedBufferPath = std::move(item.path);
			manager->runEvent("BufferLoaded");
		}
		else
		{
			GraphicsAPI::isRunning = false;
			BufferExt::destroy(item.staging);
			GraphicsAPI::isRunning = true;
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

	auto images = GraphicsAPI::imagePool.getData();
	auto imageOccupancy = GraphicsAPI::imagePool.getOccupancy();

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

				auto staging = GraphicsAPI::bufferPool.create(Buffer::Bind::TransferSrc,
					Buffer::Access::SequentialWrite, Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
				SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loadedImage" + to_string(*staging));

				auto stagingView = GraphicsAPI::bufferPool.get(staging);
				BufferExt::moveInternalObjects(item.staging, **stagingView);
				graphicsSystem->startRecording(CommandBufferType::TransferOnly);
				Image::copy(staging, item.instance);
				graphicsSystem->stopRecording();
				GraphicsAPI::bufferPool.destroy(staging);

				loadedImage = item.instance;
				loadedImagePaths = std::move(item.paths);
				manager->runEvent("ImageLoaded");
			}
			else
			{
				GraphicsAPI::isRunning = false;
				ImageExt::destroy(item.image);
				GraphicsAPI::isRunning = true;
			}
		}
		else
		{
			GraphicsAPI::isRunning = false;
			BufferExt::destroy(item.staging);
			GraphicsAPI::isRunning = true;
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
	auto manager = Manager::get();
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
namespace
{
	class ExrMemoryStream final : public Imf::IStream
	{
		const uint8* data = nullptr;
		uint64_t size = 0, offset = 0;
	public:
		ExrMemoryStream(const uint8* data, uint64_t size) : IStream("")
		{
			this->data = data;
			this->size = size;
		}

		bool isMemoryMapped() const final { return true; }

		bool read(char c[], int n)
		{
			if (n + offset > size)
				throw range_error("out of memory range");
			memcpy(c, data + offset, n); offset += n;
			return offset < size;
		}
		char* readMemoryMapped(int n) final
		{
			if (n + offset > size)
				throw range_error("out of memory range");
			auto c = data + offset; offset += n;
			return (char*)c;
		}

		uint64_t tellg() final { return offset; }
		void seekg(uint64_t pos) { offset = pos; }
	};
}

//**********************************************************************************************************************
static void loadMissingImage(vector<uint8>& data, int2& size, Image::Format& format)
{
	data.resize(sizeof(Color) * 16);
	auto pixels = (Color*)data.data();
	pixels[0] = Color::magenta; pixels[1] = Color::black;    pixels[2] = Color::magenta;  pixels[3] = Color::black;
	pixels[4] = Color::black;   pixels[5] = Color::magenta;  pixels[6] = Color::black;    pixels[7] = Color::magenta;
	pixels[8] = Color::magenta; pixels[9] = Color::black;    pixels[10] = Color::magenta; pixels[11] = Color::black;
	pixels[12] = Color::black;  pixels[13] = Color::magenta; pixels[14] = Color::black;   pixels[15] = Color::magenta;
	size = int2(4, 4);
	format = Image::Format::SrgbR8G8B8A8;
} 
static void loadMissingImage(vector<uint8>& left, vector<uint8>& right, vector<uint8>& bottom,
	vector<uint8>& top, vector<uint8>& back, vector<uint8>& front, int2& size, Image::Format& format)
{
	loadMissingImage(left, size, format);
	loadMissingImage(right, size, format);
	loadMissingImage(bottom, size, format);
	loadMissingImage(top, size, format);
	loadMissingImage(back, size, format);
	loadMissingImage(front, size, format);
} 

//**********************************************************************************************************************
void ResourceSystem::loadImageData(const fs::path& path, vector<uint8>& data,
	int2& size, Image::Format& format, int32 threadIndex) const
{
	// TODO: store images as bc compressed for polygon geometry. KTX 2.0
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	vector<uint8> dataBuffer;
	ImageFileType fileType = ImageFileType::Count; int32 fileCount = 0;
	auto imagePath = fs::path("images") / path;

	if (threadIndex < 0)
		threadIndex = 0;
	else
		threadIndex++;

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0;
	for (uint8 i = 0; i < imageFileExtCount; i++)
	{
		imagePath.replace_extension(imageFileExts[i]);
		if (packReader.getItemIndex(imagePath, itemIndex))
			fileType = imageFileTypes[i];
	}

	if (fileType == ImageFileType::Count)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Image does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(data, size, format);
		return;
	}

	packReader.readItemData(itemIndex, dataBuffer, threadIndex);
	#else
	auto filePath = appCachesPath / imagePath; filePath += ".exr";
	fileCount += fs::exists(filePath) ? 1 : 0;

	if (fileCount == 0)
	{
		for (uint8 i = 0; i < imageFileExtCount; i++)
		{
			imagePath.replace_extension(imageFileExts[i]);
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileType = imageFileTypes[i];
				fileCount++;
			}
		}
		
		if (fileCount > 1)
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Ambiguous image file extension. (path: " + path.generic_string() + ")");
			loadMissingImage(data, size, format);
			return;
		}
		else if (fileCount == 0)
		{
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

			if (fileCount != 1)
			{
				auto logSystem = Manager::get()->tryGet<LogSystem>();
				if (logSystem)
				{
					if (fileCount == 0)
						logSystem->error("Image file does not exist. (path: " + path.generic_string() + ")");
					else
						logSystem->error("Image file is ambiguous. (path: " + path.generic_string() + ")");
				}
				loadMissingImage(data, size, format);
				return;
			}
		}
	}
	else
	{
		fileType = ImageFileType::Exr;
	}

	File::loadBinary(filePath, dataBuffer);
	#endif

	loadImageData(dataBuffer.data(), dataBuffer.size(), fileType, data, size, format);

	#if GARDEN_DEBUG
	auto logSystem = Manager::get()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded image. (path: " + path.generic_string() + ")");
	#endif
}

#if !GARDEN_PACK_RESOURCES
//**********************************************************************************************************************
static void writeExrImageData(const fs::path& filePath, int32 size, const vector<uint8>& data)
{
	vector<Imf::Rgba> halfPixels(data.size());
	auto floatCount = data.size() / sizeof(float4);
	auto floatPixels = (const float4*)data.data();

	for (psize i = 0; i < floatCount; i++)
	{
		auto pixel = floatPixels[i];
		halfPixels[i] = Imf::Rgba(pixel.x, pixel.y, pixel.z, pixel.w);
	} 

	auto pathString = filePath.generic_string();
	Imf::RgbaOutputFile outputFile(pathString.c_str(), size, size, Imf::WRITE_RGBA, 1.0f,
		Imath::V2f(0.0f, 0.0f), 1.0f, Imf::INCREASING_Y, Imf::ZIP_COMPRESSION, 1);
	outputFile.setFrameBuffer(halfPixels.data(), 1, size);
	outputFile.writePixels(size);
}
#endif

//**********************************************************************************************************************
void ResourceSystem::loadCubemapData(const fs::path& path, vector<uint8>& left,
	vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top, vector<uint8>& back,
	vector<uint8>& front, int2& size, Image::Format& format, int32 threadIndex) const
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	vector<uint8> equiData; int2 equiSize;
	auto threadSystem = Manager::get()->tryGet<ThreadSystem>(); // Do not optimize this getter.

	#if !GARDEN_PACK_RESOURCES
	auto filePath = appCachesPath / "images" / path;
	auto cacheFilePath = filePath.generic_string();
	
	// TODO: also check for data timestamp against original.
	if (!fs::exists(cacheFilePath + "-nx.exr") || !fs::exists(cacheFilePath + "-px.exr") ||
		!fs::exists(cacheFilePath + "-ny.exr") || !fs::exists(cacheFilePath + "-py.exr") ||
		!fs::exists(cacheFilePath + "-nz.exr") || !fs::exists(cacheFilePath + "-pz.exr"))
	{
		loadImageData(path, equiData, equiSize, format, threadIndex);

		auto cubemapSize = equiSize.x / 4;
		if (equiSize.x / 2 != equiSize.y)
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Invalid equi cubemap size. (path: " + path.generic_string() + ")");
			loadMissingImage(left, right, bottom, top, back, front, size, format);
			return;
		}
		if (cubemapSize % 32 != 0)
		{
			auto logSystem = Manager::get()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Invalid cubemap size. (path: " + path.generic_string() + ")");
			loadMissingImage(left, right, bottom, top, back, front, size, format);
			return;
		}
		
		vector<float4> floatData; const float4* equiPixels;
		if (format == Image::Format::SrgbR8G8B8A8)
		{
			floatData.resize(equiData.size() / sizeof(Color));
			auto dstData = floatData.data();
			auto srcData = (const Color*)equiData.data();

			if (threadIndex < 0 && threadSystem)
			{
				auto& threadPool = threadSystem->getForegroundPool();
				threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
				{
					auto itemCount = task.getItemCount();
					for (uint32 i = task.getItemOffset(); i < itemCount; i++)
					{
						auto srcColor = (float4)srcData[i];
						dstData[i] = float4(pow((float3)srcColor, float3(2.2f)), srcColor.w);
					}
				}),
				(uint32)floatData.size());
				threadPool.wait();
			}
			else
			{
				for (uint32 i = 0; i < (uint32)floatData.size(); i++)
				{
					auto srcColor = (float4)srcData[i];
					dstData[i] = float4(pow((float3)srcColor, float3(2.2f)), srcColor.w);
				}
			}

			equiPixels = floatData.data();
			format = Image::Format::SfloatR32G32B32A32;
		}
		else
		{
			equiPixels = (float4*)equiData.data();
		}

		auto invDim = 1.0f / cubemapSize;
		auto equiSizeMinus1 = equiSize - 1;
		auto pixelsSize = cubemapSize * cubemapSize * sizeof(float4);
		left.resize(pixelsSize); right.resize(pixelsSize);
		bottom.resize(pixelsSize); top.resize(pixelsSize);
		back.resize(pixelsSize); front.resize(pixelsSize);
		size = int2(equiSize.x / 4, equiSize.y / 2);

		float4* cubePixelArray[6] =
		{
			(float4*)right.data(), (float4*)left.data(),
			(float4*)top.data(), (float4*)bottom.data(),
			(float4*)front.data(), (float4*)back.data(),
		};

		if (threadIndex < 0 && threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
			{
				auto sizeXY = cubemapSize * cubemapSize;
				auto itemCount = task.getItemCount();

				for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				{
					int3 coords;
					coords.z = i / sizeXY;
					coords.y = (i - coords.z * sizeXY) / cubemapSize;
					coords.x = i - (coords.y * cubemapSize + coords.z * sizeXY);
					auto cubePixels = cubePixelArray[coords.z];

					Equi2Cube::convert(coords, cubemapSize, equiSize,
						equiSizeMinus1, equiPixels, cubePixels, invDim);
				}
			}),
			cubemapSize * cubemapSize * 6);
			threadPool.wait();
		}
		else
		{
			for (uint8 face = 0; face < 6; face++)
			{
				auto cubePixels = cubePixelArray[face];
				for (int32 y = 0; y < cubemapSize; y++)
				{
					for (int32 x = 0; x < cubemapSize; x++)
					{
						Equi2Cube::convert(int3(x, y, face), cubemapSize,
							equiSize, equiSizeMinus1, equiPixels, cubePixels, invDim);
					}
				}
			}
		}

		fs::create_directories(filePath.parent_path());
		if (threadIndex < 0 && threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
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
			}), 6);
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

		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->trace("Converted spherical cubemap. (path: " + path.generic_string() + ")");
		return;
	}
	#endif

	int2 leftSize, rightSize, bottomSize, topSize, backSize, frontSize;
	Image::Format leftFormat, rightFormat, bottomFormat, topFormat, backFormat, frontFormat;

	if (threadIndex < 0 && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addTasks(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
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
		}), 6);
		threadPool.wait();
	}
	else
	{
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
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Invalid cubemap size. (path: " + path.generic_string() + ")");
	}
	if (leftSize.x != leftSize.y || rightSize.x != rightSize.y ||
		bottomSize.x != bottomSize.y || topSize.x != topSize.y ||
		backSize.x != backSize.y || frontSize.x != frontSize.y)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Invalid cubemap side size. (path: " + path.generic_string() + ")");
	}
	if (leftFormat != rightFormat || leftFormat != bottomFormat ||
		leftFormat != topFormat || leftFormat != backFormat || leftFormat != frontFormat)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Invalid cubemap format. (path: " + path.generic_string() + ")");
	}
	#endif

	size = leftSize;
	format = leftFormat;
}

//**********************************************************************************************************************
void ResourceSystem::loadImageData(const uint8* data, psize dataSize, ImageFileType fileType,
	vector<uint8>& pixels, int2& imageSize, Image::Format& format)
{
	if (fileType == ImageFileType::Webp)
	{
		if (!WebPGetInfo(data, dataSize, &imageSize.x, &imageSize.y))
			throw runtime_error("Invalid WebP image info.");
		pixels.resize(sizeof(Color) * imageSize.x * imageSize.y);
		auto decodeResult = WebPDecodeRGBAInto(data, dataSize,
			pixels.data(), pixels.size(), (int)(imageSize.x * sizeof(Color)));
		if (!decodeResult)
			throw runtime_error("Invalid WebP image data.");
	}
	else if (fileType == ImageFileType::Png)
	{
		png_image image;
		memset(&image, 0, (sizeof image));
		image.version = PNG_IMAGE_VERSION;

		if (!png_image_begin_read_from_memory(&image, data, dataSize))
			throw runtime_error("Invalid PNG image info.");

		image.format = PNG_FORMAT_RGBA;
		imageSize = int2(image.width, image.height);
		pixels.resize(PNG_IMAGE_SIZE(image));

		if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
			throw runtime_error("Invalid PNG image data.");
	}
	else if (fileType == ImageFileType::Exr)
	{
		ExrMemoryStream memoryStream(data, (uint64)dataSize);
		Imf::InputFile inputFile(memoryStream, 1);
		auto dataWindow = inputFile.header().dataWindow();
		imageSize = int2(dataWindow.max.x - dataWindow.min.x + 1,
			dataWindow.max.y - dataWindow.min.y + 1);
		auto pixelCount = (psize)imageSize.x * imageSize.y;
		pixels.resize(pixelCount * sizeof(float4));

		Imf::FrameBuffer frameBuffer;
		frameBuffer.insert("R", Imf::Slice(Imf::FLOAT, (char*)pixels.data() +
			sizeof(float) * 0, sizeof(float4), imageSize.x * sizeof(float4)));
		frameBuffer.insert("G", Imf::Slice(Imf::FLOAT, (char*)pixels.data() +
			sizeof(float) * 1, sizeof(float4), imageSize.x * sizeof(float4)));
		frameBuffer.insert("B", Imf::Slice(Imf::FLOAT, (char*)pixels.data() +
			sizeof(float) * 2, sizeof(float4), imageSize.x * sizeof(float4)));
		frameBuffer.insert("A", Imf::Slice(Imf::FLOAT, (char*)pixels.data() +
			sizeof(float) * 3, sizeof(float4), imageSize.x * sizeof(float4)));
		inputFile.setFrameBuffer(frameBuffer);
		inputFile.readPixels(dataWindow.min.y, dataWindow.max.y);

		auto pixelData = (float4*)pixels.data();
		for (psize i = 0; i < pixelCount; i++)
		{
			auto pixel = pixelData[i];
			pixelData[i] = min(pixel, float4(65504.0f));
		}
	}
	else if (fileType == ImageFileType::Jpg || fileType == ImageFileType::Bmp ||
		fileType == ImageFileType::Psd || fileType == ImageFileType::Tga)
	{
		auto pixelData = (uint8*)stbi_load_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid JPG image data.");
		pixels.resize(sizeof(Color) * imageSize.x * imageSize.y);
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else if (fileType == ImageFileType::Hdr)
	{
		auto pixelData = (uint8*)stbi_loadf_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid HDR image data.");
		pixels.resize(sizeof(float4) * imageSize.x * imageSize.y);
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else abort();

	switch (fileType)
	{
	case garden::ImageFileType::Exr:
		format = Image::Format::SfloatR32G32B32A32;
		break;
	default:
		format = Image::Format::SrgbR8G8B8A8;
		break;
	}
}

//**********************************************************************************************************************
static void loadImageArrayData(ResourceSystem* resourceSystem, const vector<fs::path>& paths,
	vector<vector<uint8>>& pixelArrays, int2& size, Image::Format& format, int32 threadIndex)
{
	resourceSystem->loadImageData(paths[0], pixelArrays[0], size, format, threadIndex);

	for (int32 i = 1; i < (int32)paths.size(); i++)
	{
		int2 elementSize; Image::Format elementFormat;
		resourceSystem->loadImageData(paths[i], pixelArrays[i], elementSize, elementFormat, threadIndex);

		if (size != elementSize || format != elementFormat)
		{
			auto count = size.x * size.y;
			pixelArrays[i].resize(toBinarySize(format) * count);

			if (format == Image::Format::SfloatR32G32B32A32)
			{
				auto pixels = (float4*)pixelArrays[i].data();
				for (int32 i = 0; i < count; i++)
					pixels[i] = float4(1.0f, 0.0f, 1.0f, 1.0f); // TODO: or maybe use checkboard pattern?
			}
			else
			{
				auto pixels = (Color*)pixelArrays[i].data();
				for (int32 i = 0; i < count; i++)
					pixels[i] = Color::magenta;
			}
		}
	}
}
static void calcLoadedImageDim(psize pathCount, int2 realSize,
	ImageLoadFlags flags, int2& imageSize, int32& layerCount) noexcept
{
	imageSize = realSize;
	layerCount = (int32)pathCount;

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
static Image::Type calcLoadedImageType(psize pathCount, int32 sizeY, ImageLoadFlags flags) noexcept
{
	if (pathCount > 1 || hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::ArrayType))
		return sizeY == 1 ? Image::Type::Texture1DArray : Image::Type::Texture2DArray;
	return sizeY == 1 ? Image::Type::Texture1D : Image::Type::Texture2D;
}
static uint8 calcLoadedImageMipCount(uint8 maxMipCount, int2 imageSize) noexcept
{
	return maxMipCount == 0 ? calcMipCount(imageSize) : std::min(maxMipCount, calcMipCount(imageSize));
}
static void copyLoadedImageData(const vector<vector<uint8>>& pixelArrays, uint8* stagingMap,
	int2 realSize, int2 imageSize, psize formatBinarySize, ImageLoadFlags flags) noexcept
{
	psize mapOffset = 0;
	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray) && realSize.x > realSize.y)
	{
		auto pixels = pixelArrays[0].data();
		auto layerCount = realSize.x / realSize.y;
		auto lineSize = formatBinarySize * imageSize.x;
		int32 offsetX = 0;

		for (int32 l = 0; l < layerCount; l++)
		{
			for (int32 y = 0; y < imageSize.y; y++)
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
		for (int32 i = 0; i < (int32)pixelArrays.size(); i++)
		{
			const auto& pixels = pixelArrays[i];
			memcpy(stagingMap + mapOffset, pixels.data(), pixels.size());
			mapOffset += pixels.size();
		}
	}
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
	
	string debugName = hasAnyFlag(flags, ImageLoadFlags::LoadShared) ? "shared." : "";
	#endif

	Hash128 hash;
	if (hasAnyFlag(flags, ImageLoadFlags::LoadShared))
	{
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
			auto imageView = GraphicsSystem::get()->get(result->second);
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
	
	auto version = GraphicsAPI::imageVersion++;
	auto image = GraphicsAPI::imagePool.create(bind, strategy, version);

	#if GARDEN_DEBUG
	auto resource = GraphicsAPI::imagePool.get(image);
	if (paths.size() > 1 || hasAnyFlag(flags, ImageLoadFlags::LoadArray))
		resource->setDebugName("imageArray." + debugName + paths[0].generic_string());
	else
		resource->setDebugName("image." + debugName + paths[0].generic_string());
	#endif

	auto threadSystem = Manager::get()->tryGet<ThreadSystem>(); // Do not optimize this getter.
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

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
			auto& paths = data->paths;
			vector<vector<uint8>> pixelArrays(paths.size()); int2 realSize; Image::Format format;
			loadImageArrayData(this, paths, pixelArrays, realSize, format, task.getThreadIndex());
			
			if (hasAnyFlag(data->flags, ImageLoadFlags::LinearData) && format == Image::Format::SrgbR8G8B8A8)
				format = Image::Format::UnormR8G8B8A8;

			auto formatBinarySize = toBinarySize(format);
			int2 imageSize; int32 layerCount;
			calcLoadedImageDim(paths.size(), realSize, data->flags, imageSize, layerCount);
			auto type = calcLoadedImageType(paths.size(), realSize.y, data->flags);
			auto mipCount = calcLoadedImageMipCount(data->maxMipCount, imageSize);

			ImageQueueItem item =
			{
				ImageExt::create(type, format, data->bind, data->strategy, 
					int3(imageSize, 1), mipCount, layerCount, data->version),
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
		}));
	}
	else
	{
		vector<vector<uint8>> pixelArrays(paths.size()); int2 realSize; Image::Format format;
		loadImageArrayData(this, paths, pixelArrays, realSize, format, -1);

		if (hasAnyFlag(flags, ImageLoadFlags::LinearData) && format == Image::Format::SrgbR8G8B8A8)
			format = Image::Format::UnormR8G8B8A8;

		auto formatBinarySize = toBinarySize(format);
		int2 imageSize; int32 layerCount;
		calcLoadedImageDim(paths.size(), realSize, flags, imageSize, layerCount);
		auto type = calcLoadedImageType(paths.size(), realSize.y, flags);
		auto mipCount = calcLoadedImageMipCount(maxMipCount, imageSize);

		auto imageInstance = ImageExt::create(type, format, bind, strategy,
			int3(imageSize, 1), mipCount, layerCount, 0);
		auto imageView = GraphicsAPI::imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto graphicsSystem = GraphicsSystem::get();
		auto staging = GraphicsAPI::bufferPool.create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, formatBinarySize * realSize.x * realSize.y, 0);
		SET_RESOURCE_DEBUG_NAME(staging, "buffer.staging.loadedImage" + to_string(*staging));
		auto stagingView = GraphicsAPI::bufferPool.get(staging);

		copyLoadedImageData(pixelArrays, stagingView->getMap(),
			realSize, imageSize, formatBinarySize, flags);
		stagingView->flush();

		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		Image::copy(staging, image);
		graphicsSystem->stopRecording();
		GraphicsAPI::bufferPool.destroy(staging);

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
		GraphicsSystem::get()->destroy(ID<Image>(image));
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
		GraphicsSystem::get()->destroy(ID<Buffer>(buffer));
}

//**********************************************************************************************************************
Ref<DescriptorSet> ResourceSystem::createSharedDescriptorSet(const Hash128& hash, 
	ID<GraphicsPipeline> graphicsPipeline, map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchrResult = sharedDescriptorSets.find(hash);
	if (searchrResult != sharedDescriptorSets.end())
		return searchrResult->second;

	auto graphicsSystem = GraphicsSystem::get();
	auto descriptorSet = graphicsSystem->createDescriptorSet(graphicsPipeline, std::move(uniforms), index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT(result.second); // Corrupted shared descriptor sets array.
	return sharedDescriptorSet;
}
Ref<DescriptorSet> ResourceSystem::createSharedDescriptorSet(const Hash128& hash, 
	ID<ComputePipeline> computePipeline, map<string, DescriptorSet::Uniform>&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(computePipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchrResult = sharedDescriptorSets.find(hash);
	if (searchrResult != sharedDescriptorSets.end())
		return searchrResult->second;

	auto graphicsSystem = GraphicsSystem::get();
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
		GraphicsSystem::get()->destroy(ID<DescriptorSet>(descriptorSet));
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
		throw runtime_error("Graphics shader file does not exist or it is ambiguous. ("
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

		auto logSystem = Manager::get()->tryGet<LogSystem>();

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
			if (logSystem)
			{
				logSystem->error("Failed to compile graphics shaders. ("
					"name: " + data.shaderPath.generic_string() + ")");
			}
			return false;
		}
		
		if (!compileResult)
			throw runtime_error("Shader files does not exist. (path: " + data.shaderPath.generic_string() + ")");
		if (logSystem)
			logSystem->trace("Compiled graphics shaders. (path: " + data.shaderPath.generic_string() + ")");

		return true;
	}
	#endif

	try
	{
		Compiler::loadGraphicsShaders(data);
	}
	catch (const exception& e)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->error("Failed to load graphics shaders. ("
				"name: " + data.shaderPath.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> ResourceSystem::loadGraphicsPipeline(const fs::path& path,
	ID<Framebuffer> framebuffer, bool useAsyncRecording, bool loadAsync, uint8 subpassIndex,
	uint32 maxBindlessCount, const map<string, GraphicsPipeline::SpecConstValue>& specConstValues,
	const map<string, GraphicsPipeline::SamplerState>& samplerStateOverrides,
	const map<uint8, GraphicsPipeline::State>& stateOverrides)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(framebuffer);

	// TODO: validate specConstValues, stateOverrides and samplerStateOverrides

	auto& framebufferView = **GraphicsAPI::framebufferPool.get(framebuffer);
	const auto& subpasses = framebufferView.getSubpasses();

	GARDEN_ASSERT((subpasses.empty() && subpassIndex == 0) ||
		(!subpasses.empty() && subpassIndex < subpasses.size()));

	auto version = GraphicsAPI::graphicsPipelineVersion++;
	auto pipeline = GraphicsAPI::graphicsPipelinePool.create(path,
		maxBindlessCount, useAsyncRecording, version, framebuffer, subpassIndex);

	auto renderPass = FramebufferExt::getRenderPass(framebufferView);
	if (renderPass)
		GraphicsAPI::renderPasses.at(renderPass)++;

	vector<Image::Format> colorFormats;
	if (subpasses.empty())
	{
		const auto& colorAttachments = framebufferView.getColorAttachments();
		colorFormats.resize(colorAttachments.size());
		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			auto attachment = GraphicsAPI::imageViewPool.get(colorAttachments[i].imageView);
			colorFormats[i] = attachment->getFormat();
		}
	}
	else
	{
		const auto& outputAttachments = subpasses[subpassIndex].outputAttachments;
		if (!outputAttachments.empty())
		{
			auto attachment = GraphicsAPI::imageViewPool.get(
				outputAttachments[outputAttachments.size() - 1].imageView);
			colorFormats.resize(isFormatColor(attachment->getFormat()) ?
				(uint32)outputAttachments.size() : (uint32)outputAttachments.size() - 1);
		}
	}

	auto depthStencilFormat = Image::Format::Undefined;
	if (framebufferView.getDepthStencilAttachment().imageView)
	{
		auto attachment = GraphicsAPI::imageViewPool.get(
			framebufferView.getDepthStencilAttachment().imageView);
		depthStencilFormat = attachment->getFormat();
	}

	auto threadSystem = Manager::get()->tryGet<ThreadSystem>(); // Do not optimize this getter.
	if (loadAsync && threadSystem)
	{
		auto data = new GraphicsPipelineLoadData();
		data->shaderPath = path;
		data->version = version;
		data->renderPass = renderPass;
		data->subpassIndex = subpassIndex;
		data->colorFormats = std::move(colorFormats);
		data->specConstValues = specConstValues;
		data->samplerStateOverrides = samplerStateOverrides;
		data->stateOverrides = stateOverrides;
		data->instance = pipeline;
		data->maxBindlessCount = maxBindlessCount;
		data->depthStencilFormat = depthStencilFormat;
		data->subpassIndex = subpassIndex;
		data->useAsyncRecording = useAsyncRecording;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachesPath = appCachesPath;
		#endif
		
		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
			Compiler::GraphicsData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			pipelineData.colorFormats = std::move(data->colorFormats);
			pipelineData.stateOverrides = std::move(data->stateOverrides);
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
		}));
	}
	else
	{
		vector<uint8> vertexCode, fragmentCode;
		Compiler::GraphicsData pipelineData;
		pipelineData.shaderPath = path;
		pipelineData.specConstValues = specConstValues;
		pipelineData.samplerStateOverrides = samplerStateOverrides;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;
		pipelineData.colorFormats = std::move(colorFormats);
		pipelineData.stateOverrides = stateOverrides;
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

		if (!loadOrCompileGraphics(pipelineData)) abort();
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(pipeline);
		GraphicsPipelineExt::moveInternalObjects(graphicsPipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->trace("Loaded graphics pipeline. (path: " +  path.generic_string() + ")");
		#endif
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
		throw runtime_error("Compute shader file does not exist, or it is ambiguous.");

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

		auto logSystem = Manager::get()->tryGet<LogSystem>();

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
			if (logSystem)
			{
				logSystem->error("Failed to compile compute shader. ("
					"name: " + data.shaderPath.generic_string() + ")");
			}
			return false;
		}
		
		if (!compileResult)
			throw runtime_error("Shader file does not exist. (path: " + data.shaderPath.generic_string() + ")");
		if (logSystem)
			logSystem->trace("Compiled compute shader. (path: " + data.shaderPath.generic_string() + ")");

		return true;
	}
	#endif

	try
	{
		Compiler::loadComputeShader(data);
	}
	catch (const exception& e)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->error("Failed to load compute shader. ("
				"name: " + data.shaderPath.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<ComputePipeline> ResourceSystem::loadComputePipeline(const fs::path& path,
	bool useAsyncRecording, bool loadAsync, uint32 maxBindlessCount,
	const map<string, Pipeline::SpecConstValue>& specConstValues,
	const map<string, GraphicsPipeline::SamplerState>& samplerStateOverrides)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto version = GraphicsAPI::computePipelineVersion++;
	auto pipeline = GraphicsAPI::computePipelinePool.create(path, maxBindlessCount, useAsyncRecording, version);

	auto threadSystem = Manager::get()->tryGet<ThreadSystem>(); // Do not optimize this getter.
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

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
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
		}));
	}
	else
	{
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
		
		if (!loadOrCompileCompute(pipelineData)) abort();

		auto computePipeline = ComputePipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = GraphicsAPI::computePipelinePool.get(pipeline);
		ComputePipelineExt::moveInternalObjects(computePipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->trace("Loaded compute pipeline. (path: " + path.generic_string() + ")");
		#endif
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
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Scene file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(scenePath);
	}
	catch (exception& e)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->trace("Failed to deserialize scene. ("
				"path: " + path.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return {};
	}
	#endif

	auto manager = Manager::get();
	TransformSystem* transformSystem = nullptr;
	ID<Entity> rootEntity = {};

	if (addRootEntity)
	{
		transformSystem = manager->tryGet<TransformSystem>();
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
						auto transformView = transformSystem->tryGet(entity);
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

	#if GARDEN_DEBUG
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded scene. (path: " + path.generic_string() + ")");
	#endif

	return rootEntity;
}

void ResourceSystem::clearScene()
{
	auto manager = Manager::get();
	auto transformSystem = manager->tryGet<TransformSystem>();
	const auto& entities = manager->getEntities();
	auto entityOccupancy = entities.getOccupancy();
	auto entityData = entities.getData();

	for (uint32 i = 0; i < entityOccupancy; i++)
	{
		auto entityView = &entityData[i];
		auto entityID = entities.getID(entityView);
		if (entityView->getComponents().empty() || manager->has<DoNotDestroyComponent>(entityID))
			continue;

		if (transformSystem)
		{
			auto transformView = transformSystem->tryGet(entityID);
			if (transformView)
				transformView->setParent({});
		}
		manager->destroy(entityID);
	}

	#if GARDEN_DEBUG
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Cleaned scene.");
	#endif
}

#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
//**********************************************************************************************************************
void ResourceSystem::storeScene(const fs::path& path, ID<Entity> rootEntity)
{
	GARDEN_ASSERT(!path.empty());

	auto scenesPath = appResourcesPath / "scenes";
	auto directoryPath = scenesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = scenesPath / path; filePath += ".scene";
	JsonSerializer serializer(filePath);

	auto manager = Manager::get();
	auto transformSystem = manager->tryGet<TransformSystem>();
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
	
	auto dnsSystem = manager->tryGet<DoNotSerializeSystem>();
	const auto& entities = manager->getEntities();
	auto entityOccupancy = entities.getOccupancy();
	auto entityData = entities.getData();

	for (uint32 i = 0; i < entityOccupancy; i++)
	{
		const auto entityView = &entityData[i];
		auto instance = entities.getID(entityView);
		const auto& components = entityView->getComponents();

		if (components.empty())
			continue;

		View<TransformComponent> transformView = {};
		if (transformSystem)
			transformView = transformSystem->tryGet(instance);

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

	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem) 
		logSystem->trace("Stored scene. (path: " + path.generic_string() + ")");
}
#endif

//**********************************************************************************************************************
Ref<Animation> ResourceSystem::loadAnimation(const fs::path& path, bool loadShared)
{
	GARDEN_ASSERT(!path.empty());
	JsonDeserializer deserializer;

	Hash128 hash;
	if (loadShared)
	{
		auto pathString = path.generic_string();
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
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Animation file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(animationPath);
	}
	catch (exception& e)
	{
		auto logSystem = Manager::get()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->trace("Failed to deserialize scene. ("
				"path: " + path.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return {};
	}
	#endif

	auto animationSystem = AnimationSystem::get();
	auto animation = animationSystem->createAnimation();
	auto animationView = animationSystem->get(animation);

	deserializer.read("frameRate", animationView->frameRate);
	deserializer.read("isLooped", animationView->isLooped);

	if (deserializer.beginChild("keyframes"))
	{
		const auto& componentNames = Manager::get()->getComponentNames();
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

	#if GARDEN_DEBUG
	auto logSystem = Manager::get()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded animation. (path: " + path.generic_string() + ")");
	#endif

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
		AnimationSystem::get()->destroy(ID<Animation>(animation));
}

#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
//**********************************************************************************************************************
void ResourceSystem::storeAnimation(const fs::path& path, ID<Animation> animation)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(animation);

	auto animationsPath = appResourcesPath / "animations";
	auto directoryPath = animationsPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = animationsPath / path; filePath += ".anim";
	JsonSerializer serializer(filePath);

	const auto animationView = AnimationSystem::get()->get(animation);
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

	auto logSystem = Manager::get()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Stored animation. (path: " + path.generic_string() + ")");
}
#endif

//--------------------------------------------------------------------------------------------------
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

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this](const ThreadPool::Task& task)
		{
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
		}, data));
	}
	else
	{
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

//**********************************************************************************************************************
Ref<Buffer> ResourceSystem::loadVertexBuffer(shared_ptr<Model> model, Model::Primitive primitive,
	Buffer::Bind bind, const vector<Model::Attribute::Type>& attributes,
	Buffer::Access access, Buffer::Strategy strategy, bool loadAsync)
{
	GARDEN_ASSERT(model);
	GARDEN_ASSERT(hasAnyFlag(bind, Buffer::Bind::TransferDst));

	#if GARDEN_DEBUG
	auto hasAnyAttribute = false;
	for (psize i = 0; i < attributes.size(); i++)
		hasAnyAttribute |= primitive.getAttributeIndex(attributes[i]) >= 0;
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

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this](const ThreadPool::Task& task)
		{
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
		}, data));
	}
	else
	{
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
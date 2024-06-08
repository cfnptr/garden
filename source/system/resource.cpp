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
#include "stb_image.h"

#include <fstream>

#if GARDEN_DEBUG
#include "ImfRgbaFile.h"
#include "webp/encode.h"
#endif

using namespace garden;
using namespace math::ibl;

//**********************************************************************************************************************
namespace
{
	struct ImageLoadData final
	{
		uint64 version = 0;
		fs::path path;
		ID<Image> instance = {};
		Image::Bind bind = {};
		Image::Strategy strategy = {};
		uint8 maxMipCount = 0;
		uint8 downscaleCount = 0;
		bool linearData = false;
	};
	struct PipelineLoadData
	{
		uint64 version = 0;
		fs::path shaderPath;
		fs::path resourcesPath;
		fs::path cachesPath;
		map<string, Pipeline::SpecConstValue> specConstValues;
		map<string, GraphicsPipeline::SamplerState> samplerStateOverrides;
		uint32 maxBindlessCount = 0;
	};
	struct GraphicsPipelineLoadData final : public PipelineLoadData
	{
		Image::Format depthStencilFormat = {};
		void* renderPass = nullptr;
		vector<Image::Format> colorFormats;
		map<uint8, GraphicsPipeline::State> stateOverrides;
		ID<GraphicsPipeline> instance = {};
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

	auto manager = Manager::getInstance();
	manager->registerEvent("ImageLoaded");
	manager->registerEvent("BufferLoaded");

	SUBSCRIBE_TO_EVENT("Init", ResourceSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", ResourceSystem::deinit);

	#if GARDEN_DEBUG
	auto appInfoSystem = AppInfoSystem::getInstance();
	appResourcesPath = appInfoSystem->getResourcesPath();
	appCachesPath = appInfoSystem->getCachesPath();
	appVersion = appInfoSystem->getVersion();
	#else
	packReader.open("resources.pack", true, thread::hardware_concurrency() + 1);
	#endif

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
ResourceSystem::~ResourceSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", ResourceSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", ResourceSystem::deinit);

		manager->unregisterEvent("ImageLoaded");
		manager->unregisterEvent("BufferLoaded");
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

//**********************************************************************************************************************
void ResourceSystem::init()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input);
}
void ResourceSystem::deinit()
{
	while (imageQueue.size() > 0)
	{
		auto& item = imageQueue.front();
		BufferExt::destroy(item.staging);
		ImageExt::destroy(item.image);
		imageQueue.pop();
	}
	while (bufferQueue.size() > 0)
	{
		auto& item = bufferQueue.front();
		BufferExt::destroy(item.staging);
		BufferExt::destroy(item.buffer);
		bufferQueue.pop();
	}
	while (computeQueue.size() > 0)
	{
		PipelineExt::destroy(computeQueue.front().pipeline);
		computeQueue.pop();
	}
	while (graphicsQueue.size() > 0)
	{
		PipelineExt::destroy(graphicsQueue.front().pipeline);
		graphicsQueue.pop();
	}

	auto manager = Manager::getInstance();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("Input", ResourceSystem::input);
}

//**********************************************************************************************************************
void ResourceSystem::dequeuePipelines()
{
	#if GARDEN_DEBUG
	auto hasNewResources = !graphicsQueue.empty() || !computeQueue.empty();
	LogSystem* logSystem = nullptr;
	if (hasNewResources)
		logSystem = Manager::getInstance()->tryGet<LogSystem>();
	#endif

	auto graphicsPipelines = GraphicsAPI::graphicsPipelinePool.getData();
	auto graphicsOccupancy = GraphicsAPI::graphicsPipelinePool.getOccupancy();

	while (graphicsQueue.size() > 0)
	{
		auto& item = graphicsQueue.front();
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
		graphicsQueue.pop();
	}

	auto computePipelines = GraphicsAPI::computePipelinePool.getData();
	auto computeOccupancy = GraphicsAPI::computePipelinePool.getOccupancy();

	while (computeQueue.size() > 0)
	{
		auto& item = computeQueue.front();
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
		computeQueue.pop();
	}
}

//**********************************************************************************************************************
void ResourceSystem::dequeueBuffersAndImages()
{
	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();

	auto hasNewResources = !bufferQueue.empty() || !imageQueue.empty();
	if (hasNewResources)
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		BEGIN_GPU_DEBUG_LABEL("Buffers Transfer", Color::transparent);
	}	

	while (bufferQueue.size() > 0)
	{
		auto& item = bufferQueue.front();
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
			SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging, "buffer.staging.loaded" + to_string(*staging));

			auto stagingView = GraphicsAPI::bufferPool.get(staging);
			BufferExt::moveInternalObjects(item.staging, **stagingView);
			Buffer::copy(staging, item.instance);
			GraphicsAPI::bufferPool.destroy(staging);

			loadedBuffer = item.instance;
			manager->runEvent("BufferLoaded");
		}
		else
		{
			GraphicsAPI::isRunning = false;
			BufferExt::destroy(item.staging);
			GraphicsAPI::isRunning = true;
		}
		bufferQueue.pop();
	}

	if (hasNewResources)
	{
		END_GPU_DEBUG_LABEL();
		BEGIN_GPU_DEBUG_LABEL("Images Transfer", Color::transparent);
	}

	auto images = GraphicsAPI::imagePool.getData();
	auto imageOccupancy = GraphicsAPI::imagePool.getOccupancy();

	while (imageQueue.size() > 0)
	{
		auto& item = imageQueue.front();
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
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging, "buffer.staging.imageLoaded" + to_string(*staging));

				auto stagingView = GraphicsAPI::bufferPool.get(staging);
				BufferExt::moveInternalObjects(item.staging, **stagingView);
				Image::copy(staging, item.instance);
				GraphicsAPI::bufferPool.destroy(staging);

				loadedImage = item.instance;
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
		imageQueue.pop();
	}

	if (hasNewResources)
	{
		END_GPU_DEBUG_LABEL();
		graphicsSystem->stopRecording();

		loadedBuffer = {};
		loadedImage = {};
	}
}

//**********************************************************************************************************************
void ResourceSystem::input()
{
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
	vector<uint8> dataBuffer;
	ImageFileType fileType = ImageFileType::Count; int32 fileCount = 0;
	auto imagePath = fs::path("images") / path;

	if (threadIndex < 0)
		threadIndex = 0;
	else
		threadIndex++;

	#if GARDEN_DEBUG
	auto filePath = appCachesPath / imagePath; filePath += ".exr";
	fileCount += fs::exists(filePath) ? 1 : 0;

	if (fileCount == 0)
	{
		imagePath += ".webp";
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Webp;
		}
		imagePath.replace_extension(".exr");
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Exr;
		}
		imagePath.replace_extension(".png");
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Png;
		}
		imagePath.replace_extension(".jpg");
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Jpg;
		}
		imagePath.replace_extension(".jpeg");
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Jpg;
		}
		imagePath.replace_extension(".hdr");
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileCount++; fileType = ImageFileType::Hdr;
		}
		
		if (fileCount > 1)
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Ambiguous image file extension. (path: " + path.generic_string() + ")");
			loadMissingImage(data, size, format);
			return;
		}
		else if (fileCount == 0)
		{
			imagePath = fs::path("models") / path; imagePath += ".webp";
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Webp;
			}
			imagePath.replace_extension(".exr");
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Exr;
			}
			imagePath.replace_extension(".png");
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Png;
			}
			imagePath.replace_extension(".jpg");
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Jpg;
			}
			imagePath.replace_extension(".jpeg");
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Jpg;
			}
			imagePath.replace_extension(".hdr");
			if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
			{
				fileCount++; fileType = ImageFileType::Hdr;
			}

			if (fileCount != 1)
			{
				auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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
	#else
	imagePath += ".webp"; uint64 itemIndex = 0;
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Webp;
	imagePath.replace_extension(".exr");
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Exr;
	imagePath.replace_extension(".png");
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Png;
	imagePath.replace_extension(".jpg");
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Jpg;
	imagePath.replace_extension(".jpeg");
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Jpg;
	imagePath.replace_extension(".hdr");
	if (packReader.getItemIndex(imagePath, itemIndex))
		fileType = ImageFile::Hdr;

	if (fileType == ImageFile::Count)
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Image does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(data, size, format);
		return;
	}

	packReader.readItemData(itemIndex, dataBuffer, threadIndex);
	#endif

	loadImageData(dataBuffer.data(), dataBuffer.size(), fileType, data, size, format);

	#if GARDEN_DEBUG
	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded image. (path: " + path.generic_string() + ")");
	#endif
}

#if GARDEN_DEBUG
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
	vector<uint8> equiData; int2 equiSize;
	auto threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();

	#if GARDEN_DEBUG
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
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
			if (logSystem)
				logSystem->error("Invalid equi cubemap size. (path: " + path.generic_string() + ")");
			loadMissingImage(left, right, bottom, top, back, front, size, format);
			return;
		}
		if (cubemapSize % 32 != 0)
		{
			auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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

		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Invalid cubemap size. (path: " + path.generic_string() + ")");
	}
	if (leftSize.x != leftSize.y || rightSize.x != rightSize.y ||
		bottomSize.x != bottomSize.y || topSize.x != topSize.y ||
		backSize.x != backSize.y || frontSize.x != frontSize.y)
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Invalid cubemap side size. (path: " + path.generic_string() + ")");
	}
	if (leftFormat != rightFormat || leftFormat != bottomFormat ||
		leftFormat != topFormat || leftFormat != backFormat || leftFormat != frontFormat)
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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
		pixels.resize(imageSize.x * imageSize.y * sizeof(Color));
		auto decodeResult = WebPDecodeRGBAInto(data, dataSize,
			pixels.data(), pixels.size(), (int)(imageSize.x * sizeof(Color)));
		if (!decodeResult)
			throw runtime_error("Invalid WebP image data.");
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
	else if (fileType == ImageFileType::Png || fileType == ImageFileType::Jpg)
	{
		auto pixelData = (uint8*)stbi_load_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid PNG/JPG image data.");
		pixels.resize(imageSize.x * imageSize.y * sizeof(Color));
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else if (fileType == ImageFileType::Hdr)
	{
		auto pixelData = (uint8*)stbi_loadf_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid HDR image data.");
		pixels.resize(imageSize.x * imageSize.y * sizeof(float4));
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else abort();

	format = fileType == ImageFileType::Webp || fileType == ImageFileType::Png || fileType == ImageFileType::Jpg ?
		Image::Format::SrgbR8G8B8A8 : Image::Format::SfloatR32G32B32A32;
}

//**********************************************************************************************************************
Ref<Image> ResourceSystem::loadImage(const fs::path& path, Image::Bind bind, uint8 maxMipCount,
	uint8 downscaleCount, Image::Strategy strategy, bool linearData, bool loadAsync)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));

	auto version = GraphicsAPI::imageVersion++;
	auto image = GraphicsAPI::imagePool.create(bind, strategy, version);

	#if GARDEN_DEBUG
	auto resource = GraphicsAPI::imagePool.get(image);
	resource->setDebugName("image." + path.generic_string());
	#endif

	auto threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();
	if (loadAsync && threadSystem)
	{
		auto data = new ImageLoadData();
		data->version = version;
		data->path = path;
		data->instance = image;
		data->bind = bind;
		data->strategy = strategy;
		data->maxMipCount = maxMipCount;
		data->downscaleCount = downscaleCount;
		data->linearData = linearData;

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
			// TODO: store also image size and preallocate required memory.
			vector<uint8> pixels; int2 size; Image::Format format;
			loadImageData(data->path, pixels, size, format, task.getThreadIndex());

			if (data->linearData && format == Image::Format::SrgbR8G8B8A8)
				format = Image::Format::UnormR8G8B8A8;
			auto multiplier = (int32)std::exp2f(data->downscaleCount);
			auto targetSize = max(size / multiplier, int2(1));
			auto formatBinarySize = toBinarySize(format);
			auto bufferBinarySize = formatBinarySize * targetSize.x * targetSize.y;

			auto mipCount = data->maxMipCount == 0 ? calcMipCount(targetSize) :
				std::min(data->maxMipCount, calcMipCount(targetSize));
			// TODO: load cubemap image

			ImageQueueItem item =
			{
				ImageExt::create(targetSize.x == 1 ? Image::Type::Texture1D : Image::Type::Texture2D,
					format, data->bind, data->strategy, int3(targetSize, 1), mipCount, 1, data->version),
				BufferExt::create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
					Buffer::Usage::Auto, Buffer::Strategy::Speed, bufferBinarySize, 0),
				data->instance,
			};

			if (data->downscaleCount > 0)
			{
				auto pixelData = pixels.data(), mapData = item.staging.getMap();
				for (int32 y = 0; y < targetSize.y; y++)
				{
					for (int32 x = 0; x < targetSize.x; x++)
					{
						memcpy(mapData + (y * targetSize.x + x) * formatBinarySize, pixelData + 
							(y * size.x + x) * multiplier * formatBinarySize, formatBinarySize);
					}
				}
			}
			else
			{
				memcpy(item.staging.getMap(), pixels.data(), bufferBinarySize);
			}
			item.staging.flush();

			delete data;
			queueLocker.lock();
			imageQueue.push(std::move(item));
			queueLocker.unlock();
		}));
	}
	else
	{
		vector<uint8> pixels; int2 size; Image::Format format;
		loadImageData(path, pixels, size, format);

		if (linearData && format == Image::Format::SrgbR8G8B8A8)
			format = Image::Format::UnormR8G8B8A8;
		auto multiplier = (int32)std::exp2f(downscaleCount);
		auto targetSize = max(size / multiplier, int2(1));
		auto formatBinarySize = toBinarySize(format);
		auto bufferBinarySize = formatBinarySize * targetSize.x * targetSize.y;
		auto mipCount = maxMipCount == 0 ? calcMipCount(targetSize) :
			std::min(maxMipCount, calcMipCount(targetSize));
		auto imageInstance = ImageExt::create(targetSize.x == 1 ? Image::Type::Texture1D :
			Image::Type::Texture2D, format, bind, strategy, int3(targetSize, 1), mipCount, 1, 0);
		auto imageView = GraphicsAPI::imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto staging = GraphicsAPI::bufferPool.create(Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, bufferBinarySize, 0);
		SET_RESOURCE_DEBUG_NAME(GraphicsSystem::getInstance(), staging,
			"buffer.staging.imageLoaded" + to_string(*staging));
		auto stagingView = GraphicsAPI::bufferPool.get(staging);

		if (downscaleCount > 0)
		{
			auto pixelData = pixels.data();
			auto mapData = stagingView->getMap();

			for (int32 y = 0; y < targetSize.y; y++)
			{
				for (int32 x = 0; x < targetSize.x; x++)
				{
					memcpy(mapData + (y * targetSize.x + x) * formatBinarySize, pixelData + 
						(y * size.x + x) * multiplier * formatBinarySize, formatBinarySize);
				}
			}
		}
		else
		{
			memcpy(stagingView->getMap(), pixels.data(), bufferBinarySize);
		}
		stagingView->flush();

		GraphicsSystem::getInstance()->startRecording(CommandBufferType::TransferOnly);
		Image::copy(staging, image);
		GraphicsSystem::getInstance()->stopRecording();
		GraphicsAPI::bufferPool.destroy(staging);
	}

	return image;
}

//**********************************************************************************************************************
static bool loadOrCompileGraphics(Compiler::GraphicsData& data)
{
	#if GARDEN_DEBUG
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

		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();

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
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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
	auto& subpasses = framebufferView.getSubpasses();

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
		auto& colorAttachments = framebufferView.getColorAttachments();
		colorFormats.resize(colorAttachments.size());
		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			auto attachment = GraphicsAPI::imageViewPool.get(colorAttachments[i].imageView);
			colorFormats[i] = attachment->getFormat();
		}
	}
	else
	{
		auto& outputAttachments = subpasses[subpassIndex].outputAttachments;
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

	auto threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();
	if (loadAsync && threadSystem)
	{
		auto data = new GraphicsPipelineLoadData();
		data->shaderPath = path;
		data->resourcesPath = appResourcesPath;
		data->cachesPath = appCachesPath;
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
		
		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
			Compiler::GraphicsData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachesPath = std::move(data->cachesPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			pipelineData.colorFormats = std::move(data->colorFormats);
			pipelineData.stateOverrides = std::move(data->stateOverrides);
			pipelineData.renderPass = data->renderPass;
			pipelineData.subpassIndex = data->subpassIndex;
			pipelineData.depthStencilFormat = data->depthStencilFormat;
			
			#if !GARDEN_DEBUG
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#endif

			if (!loadOrCompileGraphics(pipelineData))
			{
				delete data;
				return;
			}

			GraphicsQueueItem item =
			{
				data->renderPass,
				GraphicsPipelineExt::create(pipelineData, data->useAsyncRecording),
				data->instance
			};

			delete data;
			queueLocker.lock();
			graphicsQueue.push(std::move(item));
			queueLocker.unlock();
		}));
	}
	else
	{
		vector<uint8> vertexCode, fragmentCode;
		Compiler::GraphicsData pipelineData;
		pipelineData.shaderPath = path;
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachesPath = appCachesPath;
		pipelineData.specConstValues = specConstValues;
		pipelineData.samplerStateOverrides = samplerStateOverrides;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;
		pipelineData.colorFormats = std::move(colorFormats);
		pipelineData.stateOverrides = stateOverrides;
		pipelineData.renderPass = renderPass;
		pipelineData.subpassIndex = subpassIndex;
		pipelineData.depthStencilFormat = depthStencilFormat;

		#if !GARDEN_DEBUG
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#endif

		if (!loadOrCompileGraphics(pipelineData)) abort();
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(pipeline);
		GraphicsPipelineExt::moveInternalObjects(graphicsPipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->trace("Loaded graphics pipeline. (path: " +  path.generic_string() + ")");
		#endif
	}

	return pipeline;
}

//**********************************************************************************************************************
static bool loadOrCompileCompute(Compiler::ComputeData& data)
{
	#if GARDEN_DEBUG
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

		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();

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
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
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

	auto threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();
	if (loadAsync && threadSystem)
	{
		auto data = new ComputePipelineLoadData();
		data->version = version;
		data->shaderPath = path;
		data->resourcesPath = appResourcesPath;
		data->cachesPath = appCachesPath;
		data->specConstValues = specConstValues;
		data->samplerStateOverrides = samplerStateOverrides;
		data->maxBindlessCount = maxBindlessCount;
		data->instance = pipeline;
		data->useAsyncRecording = useAsyncRecording;

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this, data](const ThreadPool::Task& task)
		{
			Compiler::ComputeData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachesPath = std::move(data->cachesPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;

			#if !GARDEN_DEBUG
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
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
			computeQueue.push(std::move(item));
			queueLocker.unlock();
		}));
	}
	else
	{
		Compiler::ComputeData pipelineData;
		pipelineData.shaderPath = path;
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachesPath = appCachesPath;
		pipelineData.specConstValues = specConstValues;
		pipelineData.samplerStateOverrides = samplerStateOverrides;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;

		#if !GARDEN_DEBUG
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#endif
		
		if (!loadOrCompileCompute(pipelineData)) abort();

		auto computePipeline = ComputePipelineExt::create(pipelineData, useAsyncRecording);
		auto pipelineView = GraphicsAPI::computePipelinePool.get(pipeline);
		ComputePipelineExt::moveInternalObjects(computePipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->trace("Loaded compute pipeline. (path: " + path.generic_string() + ")");
		#endif
	}

	return pipeline;
}

//**********************************************************************************************************************
void ResourceSystem::loadScene(const fs::path& path, bool addRootEntity)
{
	#if GARDEN_DEBUG
	fs::path filePath = "scenes" / path; filePath += ".scene"; fs::path scenePath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, scenePath))
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Scene file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return;
	}

	std::ifstream fileStream(scenePath);
	if (!fileStream.is_open())
	{
		auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
		if (logSystem)
			logSystem->error("Failed to open scene file. (path: " + path.generic_string() + ")");
		return;
	}
	
	std::stringstream buffer;
	buffer << fileStream.rdbuf();
	JsonDeserializer deserializer(string_view(buffer.str()));
	#else
	abort(); // TODO: load binary bson file from the resources, also handle case when scene does not exist
	#endif

	auto manager = Manager::getInstance();
	auto& systems = manager->getSystems();
	for (const auto& pair : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializeSystem)
			continue;
		serializeSystem->preDeserialize(deserializer);
	}

	ID<Entity> rootEntity = {};
	if (addRootEntity)
	{
		rootEntity = manager->createEntity();
		auto transformComponent = manager->add<TransformComponent>(rootEntity);
		#if GARDEN_DEBUG || GARDEN_EDITOR
		transformComponent->name = path.generic_string();
		#endif
		manager->add<DoNotSerializeComponent>(rootEntity);
	}

	if (deserializer.beginChild("entities"))
	{
		const auto& componentTypes = manager->getComponentTypes();
		auto size = deserializer.getArraySize();
		for (psize i = 0; i < size; i++)
		{
			if (!deserializer.beginArrayElement(i))
				continue;

			auto entity = manager->createEntity();

			for (const auto& pair : componentTypes)
			{
				auto system = pair.second;
				const auto& componentName = system->getComponentName();
				auto serializeSystem = dynamic_cast<ISerializable*>(system);
				if (componentName.empty() || !serializeSystem)
					continue;

				if (deserializer.beginChild(componentName))
				{
					auto component = manager->add(entity, pair.first);
					if (addRootEntity && pair.first == typeid(TransformComponent))
					{
						auto transformComponent = View<TransformComponent>(component);
						transformComponent->setParent(rootEntity);
					}

					serializeSystem->deserialize(deserializer, entity, component);
					deserializer.endChild();
				}
			}
			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	for (const auto& pair : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializeSystem)
			continue;
		serializeSystem->postDeserialize(deserializer);
	}

	#if GARDEN_DEBUG
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded scene. (path: " + path.generic_string() + ")");
	#endif
}

void ResourceSystem::clearScene()
{
	auto manager = Manager::getInstance();
	auto& entities = manager->getEntities();
	auto entityOccupancy = entities.getOccupancy();
	auto entityData = entities.getData();

	for (uint32 i = 0; i < entityOccupancy; i++)
	{
		auto entity = &entityData[i];
		auto entityID = entities.getID(entity);
		if (entity->getComponents().empty() || manager->has<DoNotDestroyComponent>(entityID))
			continue;

		auto transformComponent = manager->tryGet<TransformComponent>(entityID);
		if (transformComponent)
			transformComponent->setParent({});
		manager->destroy(entityID);
	}

	#if GARDEN_DEBUG
	auto logSystem = Manager::getInstance()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Cleaned scene.");
	#endif
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
void ResourceSystem::storeScene(const fs::path& path)
{
	auto scenesPath = appResourcesPath / "scenes";
	auto directoryPath = scenesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = scenesPath / path; filePath += ".scene";
	JsonSerializer serializer(filePath);

	auto manager = Manager::getInstance();
	auto& systems = manager->getSystems();
	for (const auto& pair : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializeSystem)
			continue;
		serializeSystem->preSerialize(serializer);
	}

	serializer.write("version", appVersion.toString3());
	serializer.beginChild("entities");
	
	auto camera = GraphicsSystem::getInstance()->camera;
	auto& entities = manager->getEntities();
	auto entityOccupancy = entities.getOccupancy();
	auto entityData = entities.getData();

	for (uint32 i = 0; i < entityOccupancy; i++)
	{
		auto& entity = entityData[i];
		auto instance = entities.getID(&entity);
		auto& components = entity.getComponents();

		if (components.empty() || instance == camera ||
			manager->has<DoNotSerializeComponent>(instance))
		{
			continue;
		}

		serializer.beginArrayElement();
		for (auto& pair : components)
		{
			auto system = pair.second.first;
			auto& componentName = system->getComponentName();
			auto serializeSystem = dynamic_cast<ISerializable*>(system);

			if (componentName.empty() || !serializeSystem)
				continue;
			
			serializer.beginChild(componentName);
			serializeSystem->serialize(serializer, instance, pair.second.second);
			serializer.endChild();
		}
		serializer.endArrayElement();
	}

	serializer.endChild();

	for (const auto& pair : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializable*>(pair.second);
		if (!serializeSystem)
			continue;
		serializeSystem->postSerialize(serializer);
	}

	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem) 
		logSystem->trace("Stored scene. (path: " + path.generic_string() + ")");
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
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.loaded" + to_string(*buffer)); // TODO: use model path for this buffer

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
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging,
			"buffer.staging.loaded" + to_string(*staging));
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
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.vertex.loaded" + to_string(*buffer)); // TODO: use model path

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
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging,
			"buffer.staging.vertexLoaded" + to_string(*staging));
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
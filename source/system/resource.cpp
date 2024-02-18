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

/*
#include "garden/system/resource.hpp"
#include "garden/graphics/equi2cube.hpp"
#include "garden/graphics/compiler.hpp"
#include "garden/graphics/api.hpp"
#include "garden/file.hpp"
#include "math/ibl.hpp"

#include "ImfIO.h"
#include "ImfHeader.h"
#include "ImfInputFile.h"
#include "ImfFrameBuffer.h"
#include "stb_image.h"
#include "webp/decode.h"

#if GARDEN_DEBUG
#include "ImfRgbaFile.h"
#include "webp/encode.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#endif

using namespace garden;
using namespace math::ibl;

static bool getResourceFilePath(const fs::path& resourcePath, fs::path& filePath)
{
	auto enginePath = GARDEN_RESOURCES_PATH / resourcePath;
	auto appPath = GARDEN_APP_RESOURCES_PATH / resourcePath;
	auto hasEngineFile = fs::exists(enginePath), hasAppFile = fs::exists(appPath);
	if ((hasEngineFile && hasAppFile) || (!hasEngineFile && !hasAppFile))
		return false;
	filePath = hasEngineFile ? enginePath : appPath;
	return true;
}

//--------------------------------------------------------------------------------------------------
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
	struct GraphicsPipelineLoadData final
	{
		uint64 version = 0;
		fs::path path;
		void* renderPass = nullptr;
		vector<Image::Format> colorFormats;
		map<string, GraphicsPipeline::SpecConst> specConsts;
		map<uint8, GraphicsPipeline::State> stateOverrides;
		ID<GraphicsPipeline> instance = {};
		uint32 maxBindlessCount = 0;
		Image::Format depthStencilFormat = {};
		uint8 subpassIndex = 0;
		bool useAsync = false;
	};
	struct ComputePipelineLoadData final
	{
		uint64 version = 0;
		fs::path path;
		map<string, GraphicsPipeline::SpecConst> specConsts;
		uint32 maxBindlessCount = 0;
		ID<ComputePipeline> instance = {};
		bool useAsync = false;
	};
	struct GeneralBufferLoadData final
	{
		uint64 version = 0;
		shared_ptr<Model> model;
		ID<Buffer> instance = {};
		ModelData::Accessor accessor;
		Buffer::Bind bind = {};
		Buffer::Access access = {};
		Buffer::Strategy strategy = {};

		GeneralBufferLoadData(ModelData::Accessor _accessor) :
			accessor(_accessor) { }
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

		VertexBufferLoadData(ModelData::Primitive _primitive) :
			primitive(_primitive) { }
	};
}

//--------------------------------------------------------------------------------------------------
ResourceSystem* ResourceSystem::instance = nullptr;

ResourceSystem::ResourceSystem(Manager* manager) : System(manager)
{
	if (!instance)
		instance = this;

	SUBSCRIBE_TO_EVENT("PostDeinit", ResourceSystem::postDeinit);
	SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input); // TODO: move this to preinit.

	#if !GARDEN_DEBUG
	packReader.open("resources.pack", true, thread::hardware_concurrency() + 1);
	#endif
}
ResourceSystem::~ResourceSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
		UNSUBSCRIBE_FROM_EVENT("PostDeinit", ResourceSystem::postDeinit);
}

//--------------------------------------------------------------------------------------------------
void ResourceSystem::postDeinit()
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
}

//--------------------------------------------------------------------------------------------------
void ResourceSystem::input()
{
	#if GARDEN_DEBUG
	auto logSystem = getManager()->tryGet<LogSystem>();
	#endif

	queueLocker.lock();

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
				{
					logSystem->trace("Loaded graphics pipeline. (" +
						pipeline.getPath().generic_string() + ")");
				}
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
				{
					logSystem->trace("Loaded compute pipeline. (" +
						pipeline.getPath().generic_string() + ")");
				}
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

	graphicsSystem->startRecording(CommandBufferType::TransferOnly);

	{
		SET_GPU_DEBUG_LABEL("Buffers Transfer", Color::transparent);
		while (bufferQueue.size() > 0)
		{
			auto& item = bufferQueue.front();
			if (*item.instance <= GraphicsAPI::bufferPool.getOccupancy()) // getOccupancy() required.
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
			
				auto staging = GraphicsAPI::bufferPool.create(
					Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
					Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging,
					"buffer.staging.loaded" + to_string(*staging));
				auto stagingView = GraphicsAPI::bufferPool.get(staging);
				BufferExt::moveInternalObjects(item.staging, **stagingView);
				Buffer::copy(staging, item.instance);
				GraphicsAPI::bufferPool.destroy(staging);
			}
			else
			{
				GraphicsAPI::isRunning = false;
				BufferExt::destroy(item.staging);
				GraphicsAPI::isRunning = true;
			}
			bufferQueue.pop();
		}
	}

	auto images = GraphicsAPI::imagePool.getData();
	auto imageOccupancy = GraphicsAPI::imagePool.getOccupancy();

	{
		SET_GPU_DEBUG_LABEL("Images Transfer", Color::transparent);
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
				}
				else
				{
					GraphicsAPI::isRunning = false;
					ImageExt::destroy(item.image);
					GraphicsAPI::isRunning = true;
				}

				auto staging = GraphicsAPI::bufferPool.create(
					Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
					Buffer::Usage::Auto, Buffer::Strategy::Speed, 0);
				SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging,
					"buffer.staging.imageLoaded" + to_string(*staging));
				auto stagingView = GraphicsAPI::bufferPool.get(staging);
				BufferExt::moveInternalObjects(item.staging, **stagingView);
				Image::copy(staging, item.instance);
				GraphicsAPI::bufferPool.destroy(staging);
			}
			else
			{
				GraphicsAPI::isRunning = false;
				BufferExt::destroy(item.staging);
				GraphicsAPI::isRunning = true;
			}
			imageQueue.pop();
		}
	}

	graphicsSystem->stopRecording();
	queueLocker.unlock();
}

//--------------------------------------------------------------------------------------------------
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
	auto filePath = GARDEN_CACHES_PATH / imagePath; filePath += ".exr";
	fileCount += fs::exists(filePath) ? 1 : 0;

	if (fileCount == 0)
	{
		imagePath += ".webp";
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Webp; }
		imagePath.replace_extension(".exr");
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Exr; }
		imagePath.replace_extension(".png");
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Png; }
		imagePath.replace_extension(".jpg");
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Jpg; }
		imagePath.replace_extension(".jpeg");
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Jpg; }
		imagePath.replace_extension(".hdr");
		if (getResourceFilePath(imagePath, filePath))
		{ fileCount++; fileType = ImageFileType::Hdr; }
		
		if (fileCount > 1)
		{
			throw runtime_error("Ambiguous image file extension. ("
				"name: " + path.generic_string() + ")");
		}
		else if (fileCount == 0)
		{
			imagePath = fs::path("models") / path; imagePath += ".webp";
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Webp; }
			imagePath.replace_extension(".exr");
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Exr; }
			imagePath.replace_extension(".png");
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Png; }
			imagePath.replace_extension(".jpg");
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Jpg; }
			imagePath.replace_extension(".jpeg");
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Jpg; }
			imagePath.replace_extension(".hdr");
			if (getResourceFilePath(imagePath, filePath))
			{ fileCount++; fileType = ImageFileType::Hdr; }

			if (fileCount != 1)
			{
				throw runtime_error("Image file does not exist, or it is ambiguous. ("
					"path: " + path.generic_string() + ")");
			}
		}
	}
	else
	{
		fileType = ImageFileType::Exr;
	}

	loadBinaryFile(filePath, dataBuffer);
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
		throw runtime_error("Image does not exist. ("
			"path: " + path.generic_string() + ")");
	}
	packReader.readItemData(itemIndex, dataBuffer, threadIndex);
	#endif

	loadImageData(dataBuffer.data(), dataBuffer.size(), fileType, data, size, format);

	#if GARDEN_DEBUG
	auto logSystem = getManager()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded image. (" + path.generic_string() + ")");
	#endif
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------------
void ResourceSystem::loadCubemapData(const fs::path& path, vector<uint8>& left,
	vector<uint8>& right, vector<uint8>& bottom, vector<uint8>& top, vector<uint8>& back,
	vector<uint8>& front, int2& size, Image::Format& format, int32 threadIndex) const
{
	GARDEN_ASSERT(!path.empty());
	vector<uint8> equiData; int2 equiSize;

	#if GARDEN_DEBUG
	auto filePath = GARDEN_CACHES_PATH / "images" / path;
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
			throw runtime_error("Invalid equi cubemap size. ("
				"path: " + path.generic_string() + ")");
		}
		if (cubemapSize % 32 != 0)
		{
			throw runtime_error("Invalid cubemap size. ("
				"path: " + path.generic_string() + ")");
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
						auto srcColor = srcData[i].toNormFloat4();
						dstData[i] = float4(pow((float3)srcColor, 
							float3(2.2f)), srcColor.w);
					}
				}),
				(uint32)floatData.size());
				threadPool.wait();
			}
			else
			{
				for (uint32 i = 0; i < (uint32)floatData.size(); i++)
				{
					auto srcColor = srcData[i].toNormFloat4();
					dstData[i] = float4(pow((float3)srcColor,
						float3(2.2f)), srcColor.w);
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
					case 0: writeExrImageData(cacheFilePath +
						"-nx.exr", cubemapSize, left); break;
					case 1: writeExrImageData(cacheFilePath +
						"-px.exr", cubemapSize, right); break;
					case 2: writeExrImageData(cacheFilePath +
						"-ny.exr", cubemapSize, bottom); break;
					case 3: writeExrImageData(cacheFilePath +
						"-py.exr", cubemapSize, top); break;
					case 4: writeExrImageData(cacheFilePath +
						"-nz.exr", cubemapSize, back); break;
					case 5: writeExrImageData(cacheFilePath +
						"-pz.exr", cubemapSize, front); break;
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

		auto logSystem = getManager()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->trace("Converted spherical cubemap. (" +
				path.generic_string() + ")");
		}
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
			case 0: loadImageData(filePath + "-nx", left,
				leftSize, leftFormat, task.getThreadIndex()); break;
			case 1: loadImageData(filePath + "-px", right,
				rightSize, rightFormat, task.getThreadIndex()); break;
			case 2: loadImageData(filePath + "-ny", bottom,
				bottomSize, bottomFormat, task.getThreadIndex()); break;
			case 3: loadImageData(filePath + "-py", top,
				topSize, topFormat, task.getThreadIndex()); break;
			case 4: loadImageData(filePath + "-nz", back,
				backSize, backFormat, task.getThreadIndex()); break;
			case 5: loadImageData(filePath + "-pz", front,
				frontSize, frontFormat, task.getThreadIndex()); break;
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
		throw runtime_error("Invalid cubemap size. ("
			"path: " + path.generic_string() + ")");
	}
	if (leftSize.x != leftSize.y || rightSize.x != rightSize.y ||
		bottomSize.x != bottomSize.y || topSize.x != topSize.y ||
		backSize.x != backSize.y || frontSize.x != frontSize.y)
	{
		throw runtime_error("Invalid cubemap side size. ("
			"path: " + path.generic_string() + ")");
	}
	if (leftSize.x != leftSize.y || rightSize.x != rightSize.y ||
		bottomSize.x != bottomSize.y || topSize.x != topSize.y ||
		backSize.x != backSize.y || frontSize.x != frontSize.y)
	{
		throw runtime_error("Invalid cubemap side size. ("
			"path: " + path.generic_string() + ")");
	}
	if (leftFormat != rightFormat || leftFormat != bottomFormat ||
		leftFormat != topFormat || leftFormat != backFormat || leftFormat != frontFormat)
	{
		throw runtime_error("Invalid cubemap format. ("
			"path: " + path.generic_string() + ")");
	}
	#endif

	size = leftSize;
	format = leftFormat;
}

//--------------------------------------------------------------------------------------------------
void ResourceSystem::loadImageData(const uint8* data, psize dataSize, ImageFile fileType,
	vector<uint8>& pixels, int2& imageSize, Image::Format& format)
{
	if (fileType == ImageFile::Webp)
	{
		if (!WebPGetInfo(data, dataSize, &imageSize.x, &imageSize.y))
			throw runtime_error("Invalid image data.");
		pixels.resize(imageSize.x * imageSize.y * sizeof(Color));
		auto decodeResult = WebPDecodeRGBAInto(data, dataSize,
			pixels.data(), pixels.size(), (int)(imageSize.x * sizeof(Color)));
		if (!decodeResult)
			throw runtime_error("Invalid image data.");
	}
	else if (fileType == ImageFile::Exr)
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
	else if (fileType == ImageFile::Png || fileType == ImageFile::Jpg)
	{
		auto pixelData = (uint8*)stbi_load_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid image data.");
		pixels.resize(imageSize.x * imageSize.y * sizeof(Color));
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else if (fileType == ImageFile::Hdr)
	{
		auto pixelData = (uint8*)stbi_loadf_from_memory(data,
			(int)dataSize, &imageSize.x, &imageSize.y, nullptr, 4);
		if (!pixelData)
			throw runtime_error("Invalid image data.");
		pixels.resize(imageSize.x * imageSize.y * sizeof(float4));
		memcpy(pixels.data(), pixelData, pixels.size());
		stbi_image_free(pixelData);
	}
	else abort();

	format =
		fileType == ImageFile::Webp ||
		fileType == ImageFile::Png ||
		fileType == ImageFile::Jpg ?
		Image::Format::SrgbR8G8B8A8 :
		Image::Format::SfloatR32G32B32A32;
}

//--------------------------------------------------------------------------------------------------
Ref<Image> ResourceSystem::loadImage(const fs::path& path, Image::Bind bind, uint8 maxMipCount,
	uint8 downscaleCount, Image::Strategy strategy, bool linearData, bool loadAsync)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(hasAnyFlag(bind, Image::Bind::TransferDst));

	auto version = GraphicsAPI::imageVersion++;
	auto image = GraphicsAPI::imagePool.create(bind, strategy, version);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, image, "image.loaded" + to_string(*image));

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
		threadPool.addTask(ThreadPool::Task([this](const ThreadPool::Task& task)
		{
			auto data = (ImageLoadData*)task.getArgument();
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
				ImageExt::create(targetSize.x == 1 ? Image::Type::Texture1D :
					Image::Type::Texture2D, format, data->bind, data->strategy,
					int3(targetSize, 1), mipCount, 1, data->version),
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
						memcpy(mapData + (y * targetSize.x + x) * formatBinarySize,
							pixelData + (y * size.x + x) * multiplier *
							formatBinarySize, formatBinarySize);
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
		}, data));
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
		auto imageInstance = ImageExt::create(
			targetSize.x == 1 ? Image::Type::Texture1D :
			Image::Type::Texture2D, format, bind, strategy,
			int3(targetSize, 1), mipCount, 1, 0);
		auto imageView = GraphicsAPI::imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto staging = GraphicsAPI::bufferPool.create(
			Buffer::Bind::TransferSrc, Buffer::Access::SequentialWrite,
			Buffer::Usage::Auto, Buffer::Strategy::Speed, bufferBinarySize, 0);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, staging,
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
					memcpy(mapData + (y * targetSize.x + x) * formatBinarySize,
						pixelData + (y * size.x + x) * multiplier *
						formatBinarySize, formatBinarySize);
				}
			}
		}
		else
		{
			memcpy(stagingView->getMap(), pixels.data(), bufferBinarySize);
		}
		stagingView->flush();

		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		Image::copy(staging, image);
		graphicsSystem->stopRecording();
		GraphicsAPI::bufferPool.destroy(staging);
	}

	return image;
}

//--------------------------------------------------------------------------------------------------
static bool loadOrCompileGraphics(Manager* manager, Compiler::GraphicsData& data)
{
	#if GARDEN_DEBUG
	auto vertexPath = "shaders" / data.path; vertexPath += ".vert";
	auto fragmentPath = "shaders" / data.path; fragmentPath += ".frag";
	auto headerPath = "shaders" / data.path; headerPath += ".gslh";

	fs::path vertexInputPath, fragmentInputPath;
	auto hasVertexShader = getResourceFilePath(vertexPath, vertexInputPath);
	auto hasFragmentShader = getResourceFilePath(fragmentPath, fragmentInputPath);

	if (!hasVertexShader && !hasFragmentShader)
	{
		throw runtime_error("Graphics shader file does not exist or it is ambiguous. ("
			"path: " + data.path.generic_string() + ")");
	}

	vertexPath += ".spv"; fragmentPath += ".spv";
	auto vertexOutputPath = GARDEN_CACHES_PATH / vertexPath;
	auto fragmentOutputPath = GARDEN_CACHES_PATH / fragmentPath;
	auto headerFilePath = GARDEN_CACHES_PATH / headerPath;
	
	if (!fs::exists(headerFilePath) ||
		(hasVertexShader && (!fs::exists(vertexOutputPath) ||
		fs::last_write_time(vertexInputPath) > fs::last_write_time(vertexOutputPath))) ||
		(hasFragmentShader && (!fs::exists(fragmentOutputPath) ||
		fs::last_write_time(fragmentInputPath) > fs::last_write_time(fragmentOutputPath))))
	{
		const vector<fs::path> includePaths =
		{
			GARDEN_RESOURCES_PATH / "shaders",
			GARDEN_APP_RESOURCES_PATH / "shaders"
		};
		bool compileResult = false;

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

		auto logSystem = manager->tryGet<LogSystem>();

		try
		{
			auto dataPath = data.path; data.path = dataPath.filename();
			compileResult = Compiler::compileGraphicsShaders(
				inputPath, outputPath, includePaths, data);
			data.path = dataPath;
		}
		catch(const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << vertexInputPath.generic_string() << "(.frag):" << e.what() << "\n"; // TODO: get info which stage throw.

			if (logSystem)
			{
				logSystem->error("Failed to compile graphics shaders. ("
					"name: " + data.path.generic_string() + ")");
			}
			return false;
		}
		
		if (!compileResult)
		{
			throw runtime_error("Shader files does not exist. ("
				"path: " + data.path.generic_string() + ")");
		}

		if (logSystem)
		{
			logSystem->trace("Compiled graphics shaders. (" +
				data.path.generic_string() + ")");
		}
		return true;
	}
	#endif

	try
	{
		#error pass cachesPath
		Compiler::loadGraphicsShaders(data);
	}
	catch (const exception& e)
	{
		auto logSystem = manager->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->error("Failed to load graphics shaders. ("
				"name: " + data.path.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
ID<GraphicsPipeline> ResourceSystem::loadGraphicsPipeline(const fs::path& path,
	ID<Framebuffer> framebuffer, bool useAsync, bool loadAsync, uint8 subpassIndex,
	uint32 maxBindlessCount, const map<string, GraphicsPipeline::SpecConst>& specConsts,
	const map<uint8, GraphicsPipeline::State>& stateOverrides)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(framebuffer);
	auto& framebufferView = **GraphicsAPI::framebufferPool.get(framebuffer);
	auto& subpasses = framebufferView.getSubpasses();

	GARDEN_ASSERT((subpasses.empty() && subpassIndex == 0) ||
		(!subpasses.empty() && subpassIndex < subpasses.size()));

	auto version = GraphicsAPI::graphicsPipelineVersion++;
	auto pipeline = GraphicsAPI::graphicsPipelinePool.create(path,
		maxBindlessCount, useAsync, version, framebuffer, subpassIndex);

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

	if (loadAsync && threadSystem)
	{
		auto data = new GraphicsPipelineLoadData();
		data->path = path;
		data->version = version;
		data->renderPass = renderPass;
		data->subpassIndex = subpassIndex;
		data->colorFormats = std::move(colorFormats);
		data->specConsts = specConsts;
		data->stateOverrides = stateOverrides;
		data->instance = pipeline;
		data->maxBindlessCount = maxBindlessCount;
		data->depthStencilFormat = depthStencilFormat;
		data->subpassIndex = subpassIndex;
		data->useAsync = useAsync;
		
		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this](const ThreadPool::Task& task)
		{
			auto data = (GraphicsPipelineLoadData*)task.getArgument();
			Compiler::GraphicsData pipelineData;
			pipelineData.path = std::move(data->path);
			pipelineData.specConsts = std::move(data->specConsts);
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

			if (!loadOrCompileGraphics(getManager(), pipelineData))
			{
				delete data;
				return;
			}

			GraphicsQueueItem item =
			{
				data->renderPass,
				GraphicsPipelineExt::create(pipelineData, data->useAsync),
				data->instance
			};

			delete data;
			queueLocker.lock();
			graphicsQueue.push(std::move(item));
			queueLocker.unlock();
		}, data));
	}
	else
	{
		vector<uint8> vertexCode, fragmentCode;
		Compiler::GraphicsData pipelineData;
		pipelineData.path = path;
		pipelineData.specConsts = specConsts;
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

		if (!loadOrCompileGraphics(getManager(), pipelineData)) abort();
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData, useAsync);
		auto pipelineView = GraphicsAPI::graphicsPipelinePool.get(pipeline);
		GraphicsPipelineExt::moveInternalObjects(graphicsPipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = getManager()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->trace("Loaded graphics pipeline. (" + 
				path.generic_string() + ")");
		}
		#endif
	}

	return pipeline;
}

//--------------------------------------------------------------------------------------------------
static bool loadOrCompileCompute(Manager* manager, Compiler::ComputeData& data)
{
	#if GARDEN_DEBUG
	auto computePath = "shaders" / data.path; computePath += ".comp";
	fs::path computeInputPath;
	if (!getResourceFilePath(computePath, computeInputPath))
		throw runtime_error("Compute shader file does not exist, or it is ambiguous.");

	auto headerPath = "shaders" / data.path;
	computePath += ".spv"; headerPath += ".gslh";
	auto computeOutputPath = GARDEN_CACHES_PATH / computePath;
	auto headerFilePath = GARDEN_CACHES_PATH / headerPath;
	
	if (!fs::exists(headerFilePath) || !fs::exists(computeOutputPath) ||
		fs::last_write_time(computeInputPath) > fs::last_write_time(computeOutputPath))
	{
		const vector<fs::path> includePaths =
		{
			GARDEN_RESOURCES_PATH / "shaders",
			GARDEN_APP_RESOURCES_PATH / "shaders"
		};
		bool compileResult = false;

		auto logSystem = manager->tryGet<LogSystem>();

		try
		{
			auto dataPath = data.path; data.path = dataPath.filename();
			compileResult = Compiler::compileComputeShader(computeInputPath.parent_path(),
				computeOutputPath.parent_path(), includePaths, data);
			data.path = dataPath;
		}
		catch(const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << computeInputPath.generic_string() << ":" << e.what() << "\n";

			if (logSystem)
			{
				logSystem->error("Failed to compile compute shader. ("
					"name: " + data.path.generic_string() + ")");
			}
			return false;
		}
		
		if (!compileResult)
		{
			throw runtime_error("Shader file does not exist. ("
				"path: " + data.path.generic_string() + ")");
		}
		
		if (logSystem)
		{
			logSystem->trace("Compiled compute shader. (" +
				data.path.generic_string() + ")");
		}
		return true;
	}
	#endif

	try
	{
		Compiler::loadComputeShader(data);
	}
	catch (const exception& e)
	{
		auto logSystem = manager->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->error("Failed to load compute shader. ("
				"name: " + data.path.generic_string() + ", "
				"error: " + string(e.what()) + ")");
		}
		return false;
	}

	return true;
}

//--------------------------------------------------------------------------------------------------
ID<ComputePipeline> ResourceSystem::loadComputePipeline(const fs::path& path,
	bool useAsync, bool loadAsync, uint32 maxBindlessCount,
	const map<string, GraphicsPipeline::SpecConst>& specConsts)
{
	GARDEN_ASSERT(!path.empty());

	auto version = GraphicsAPI::computePipelineVersion++;
	auto pipeline = GraphicsAPI::computePipelinePool.create(
		path, maxBindlessCount, useAsync, version);

	if (loadAsync && threadSystem)
	{
		auto data = new ComputePipelineLoadData();
		data->version = version;
		data->path = path;
		data->specConsts = specConsts;
		data->maxBindlessCount = maxBindlessCount;
		data->instance = pipeline;
		data->useAsync = useAsync;

		auto& threadPool = threadSystem->getBackgroundPool();
		threadPool.addTask(ThreadPool::Task([this](const ThreadPool::Task& task)
		{
			auto data = (ComputePipelineLoadData*)task.getArgument();
			Compiler::ComputeData pipelineData;
			pipelineData.path = std::move(data->path);
			pipelineData.specConsts = std::move(data->specConsts);
			pipelineData.pipelineVersion = data->version;
			pipelineData.maxBindlessCount = data->maxBindlessCount;

			#if !GARDEN_DEBUG
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#endif
			
			if (!loadOrCompileCompute(getManager(), pipelineData))
			{
				delete data;
				return;
			}

			ComputeQueueItem item = 
			{
				ComputePipelineExt::create(pipelineData, data->useAsync),
				data->instance
			};

			delete data;
			queueLocker.lock();
			computeQueue.push(std::move(item));
			queueLocker.unlock();
		}, data));
	}
	else
	{
		Compiler::ComputeData pipelineData;
		pipelineData.path = path;
		pipelineData.specConsts = specConsts;
		pipelineData.pipelineVersion = version;
		pipelineData.maxBindlessCount = maxBindlessCount;

		#if !GARDEN_DEBUG
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#endif
		
		if (!loadOrCompileCompute(getManager(), pipelineData)) abort();

		auto computePipeline = ComputePipelineExt::create(pipelineData, useAsync);
		auto pipelineView = GraphicsAPI::computePipelinePool.get(pipeline);
		ComputePipelineExt::moveInternalObjects(computePipeline, **pipelineView);

		#if GARDEN_DEBUG
		auto logSystem = getManager()->tryGet<LogSystem>();
		if (logSystem)
		{
			logSystem->trace("Loaded compute pipeline. (" +
				path.generic_string() + ")");
		}
		#endif
	}

	return pipeline;
}

//--------------------------------------------------------------------------------------------------
void ResourceSystem::loadScene(const fs::path& path)
{
	fs::path scenePath, filePath = "scenes" / path; filePath += ".scene";
	if (!getResourceFilePath(filePath, scenePath))
		throw runtime_error("Scene file does not exist. (path: " + path.generic_string() + ")");
	conf::Reader confReader(scenePath);

	auto manager = getManager();
	auto& systems = manager->getSystems();
	uint32 index = 0;

	while (true)
	{
		auto entity = manager->createEntity();

		auto hasAnyValue = false;
		for (auto system : systems)
		{
			auto serializeSystem = dynamic_cast<ISerializeSystem*>(system);
			if (!serializeSystem)
				continue;
			hasAnyValue |= serializeSystem->deserialize(confReader, index++, entity);
		}

		if (!hasAnyValue)
		{
			manager->destroy(entity);
			break;
		}
	}

	for (auto system : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializeSystem*>(system);
		if (!serializeSystem)
			continue;
		serializeSystem->postDeserialize(confReader);
	}

	#if GARDEN_DEBUG
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded scene. (" + path.generic_string() + ")");
	#endif
}
void ResourceSystem::storeScene(const fs::path& path)
{
	auto scenesPath = GARDEN_APP_RESOURCES_PATH / "scenes";
	auto directoryPath = scenesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = scenesPath / path; filePath += ".scene";
	conf::Writer confWriter(filePath);

	confWriter.writeComment(GARDEN_APP_NAME_STRING
		" Scene (" GARDEN_APP_VERSION_STRING ")");
	
	auto manager = getManager();
	auto& entities = manager->getEntities();
	auto entityOccupancy = entities.getOccupancy();
	auto entityData = entities.getData();
	uint32 index = 0;

	for (uint32 i = 0; i < entityOccupancy; i++)
	{
		auto& entity = entityData[i];
		auto& components = entity.getComponents();

		if (components.empty() || components.find(
			typeid(DoNotDestroyComponent)) != components.end())
		{
			continue;
		}

		confWriter.writeNewLine();
		for (auto& pair : components)
		{
			auto serializeSystem = dynamic_cast<ISerializeSystem*>(pair.second.first);
			if (!serializeSystem)
				continue;
			serializeSystem->serialize(confWriter, index++, pair.second.second);
		}
	}

	auto& systems = manager->getSystems();
	for (auto system : systems)
	{
		auto serializeSystem = dynamic_cast<ISerializeSystem*>(system);
		if (!serializeSystem)
			continue;
		serializeSystem->postSerialize(confWriter);
	}

	#if GARDEN_DEBUG
	auto logSystem = manager->tryGet<LogSystem>();
	if (logSystem) 
		logSystem->trace("Stored scene. (" + path.generic_string() + ")");
	#endif
}
void ResourceSystem::clearScene()
{
	auto manager = getManager();
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
	auto logSystem = getManager()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Cleaned scene.");
	#endif
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
Model::~Model()
{
	cgltf_free((cgltf_data*)instance);
}
uint32 Model::getSceneCount() const noexcept
{
	GARDEN_ASSERT(instance);
	auto cgltf = (cgltf_data*)instance;
	return (uint32)cgltf->scenes_count;
}
Model::Scene Model::getScene(uint32 index) const noexcept
{
	GARDEN_ASSERT(instance);
	auto cgltf = (cgltf_data*)instance;
	GARDEN_ASSERT(index < cgltf->scenes_count);
	return Scene(&cgltf->scenes[index]);
}

//--------------------------------------------------------------------------------------------------
string_view Model::Scene::getName() const noexcept
{
	GARDEN_ASSERT(data);
	auto scene = (cgltf_scene*)data;
	return scene->name ? scene->name : "";
}
uint32 Model::Scene::getNodeCount() const noexcept
{
	GARDEN_ASSERT(data);
	auto scene = (cgltf_scene*)data;
	return (uint32)scene->nodes_count;
}
Model::Node Model::Scene::getNode(uint32 index) const noexcept
{
	GARDEN_ASSERT(data);
	auto scene = (cgltf_scene*)data;
	GARDEN_ASSERT(index < scene->nodes_count);
	return Node(scene->nodes[index]);
}

//--------------------------------------------------------------------------------------------------
string_view Model::Node::getName() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return node->name ? node->name : "";
}
Model::Node Model::Node::getParent() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return Node(node->parent);
}
uint32 Model::Node::getChildrenCount() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return (uint32)node->children_count;
}
Model::Node Model::Node::getChildren(uint32 index) const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	GARDEN_ASSERT(index < node->children_count);
	return Node(node->children[index]);
}
float3 Model::Node::getPosition() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	if (!node->has_translation)
	{
		if (!node->has_matrix)
			return float3(0.0f);
		auto matrix = node->matrix;
		return float3(-matrix[12], matrix[13], matrix[14]);
	}
	auto position = node->translation;
	return float3(-position[0], position[1], position[2]);
}
float3 Model::Node::getScale() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	if (!node->has_scale)
	{
		if (!node->has_matrix)
			return float3(1.0f);
		auto matrix = node->matrix;
		auto model = float4x4(
			matrix[0], matrix[4], matrix[8], matrix[12],
			matrix[1], matrix[5], matrix[9], matrix[13],
			matrix[2], matrix[6], matrix[10], matrix[14],
			matrix[3], matrix[7], matrix[11], matrix[15]);
		return extractScale(model);
	}
	auto scale = node->scale;
	return float3(scale[0], scale[1], scale[2]);
}
quat Model::Node::getRotation() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	if (!node->has_rotation)
	{
		if (!node->has_matrix)
			return quat::identity;
		auto matrix = node->matrix;
		auto model = float4x4(
			matrix[0], matrix[4], matrix[8], matrix[12],
			matrix[1], matrix[5], matrix[9], matrix[13],
			matrix[2], matrix[6], matrix[10], matrix[14],
			matrix[3], matrix[7], matrix[11], matrix[15]);
		auto rotation = extractQuat(model);
		rotation.y = -rotation.y; rotation.z = -rotation.z;
		return rotation;
	}
	auto rotation = node->rotation;
	return quat(rotation[0], -rotation[1], -rotation[2], rotation[3]);
}
bool Model::Node::hashMesh() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return node->mesh;
}
Model::Mesh Model::Node::getMesh() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return Mesh(node->mesh); // TODO: gpu instancing
}
bool Model::Node::hasCamera() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return node->camera;
}
bool Model::Node::hasLight() const noexcept
{
	GARDEN_ASSERT(data);
	auto node = (cgltf_node*)data;
	return node->light;
}

//--------------------------------------------------------------------------------------------------
string_view Model::Mesh::getName() const noexcept
{
	GARDEN_ASSERT(data);
	auto mesh = (cgltf_mesh*)data;
	return mesh->name ? mesh->name : "";
}
psize Model::Mesh::getPrimitiveCount() const noexcept
{
	GARDEN_ASSERT(data);
	auto mesh = (cgltf_mesh*)data;
	return (psize)mesh->primitives_count;
}
Model::Primitive Model::Mesh::getPrimitive(psize index) const noexcept
{
	GARDEN_ASSERT(data);
	auto mesh = (cgltf_mesh*)data;
	GARDEN_ASSERT(index < mesh->primitives_count);
	return Primitive(&mesh->primitives[index]);
}

//--------------------------------------------------------------------------------------------------
Model::Primitive::Type Model::Primitive::getType() const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	return (Type)primitive->type;
}
uint32 Model::Primitive::getAttributeCount() const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	return (uint32)primitive->attributes_count;
}
Model::Attribute Model::Primitive::getAttribute(int32 index) const noexcept
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(index >= 0);
	auto primitive = (cgltf_primitive*)data;
	GARDEN_ASSERT(index < primitive->attributes_count);
	return Attribute(&primitive->attributes[index]);
}
Model::Attribute Model::Primitive::getAttribute(Attribute::Type type) const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	auto attributes = primitive->attributes;
	auto count = (uint32)primitive->attributes_count;

	for (uint32 i = 0; i < count; i++)
	{
		if (attributes[i].type != (cgltf_attribute_type)type)
			continue;
		return Model::Attribute(&attributes[i]);
	}

	abort();
}
int32 Model::Primitive::getAttributeIndex(Attribute::Type type) const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	auto attributes = primitive->attributes;
	auto count = (int32)primitive->attributes_count;

	for (int32 i = 0; i < count; i++)
	{
		if (attributes[i].type == (cgltf_attribute_type)type)
			return i;
	}

	return -1;
}
Model::Accessor Model::Primitive::getIndices() const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	return Accessor(primitive->indices);
}
bool Model::Primitive::hasMaterial() const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	return primitive->material && primitive->material->has_pbr_metallic_roughness;
}
Model::Material Model::Primitive::getMaterial() const noexcept
{
	GARDEN_ASSERT(data);
	auto primitive = (cgltf_primitive*)data;
	return Material(primitive->material);
}
psize Model::Primitive::getVertexCount(
	const vector<Attribute::Type>& attributes) const noexcept
{
	GARDEN_ASSERT(data);
	psize vertexCount = 0;
	for (psize i = 0; i < (psize)attributes.size(); i++)
	{
		auto attributeIndex = getAttributeIndex(attributes[i]);
		if (attributeIndex < 0)
			continue;
		auto count = getAttribute(attributeIndex).getAccessor().getCount();
		if (count > vertexCount)
			vertexCount = count;
	}
	return vertexCount;
}
psize Model::Primitive::getBinaryStride(
	const vector<Attribute::Type>& attributes) noexcept
{
	GARDEN_ASSERT(!attributes.empty());
	psize stride = 0;
	for (uint32 i = 0; i < (uint32)attributes.size(); i++)
		stride += toBinarySize(attributes[i]);
	return stride;
}
void Model::Primitive::copyVertices(const vector<Attribute::Type>& attributes,
	uint8* _destination, psize count, psize offset) const noexcept
{
	GARDEN_ASSERT(data);
	auto vertexCount = getVertexCount(attributes);

	GARDEN_ASSERT((count == 0 && offset == 0) ||
		(count + offset <= vertexCount));
	if (count == 0)
		count = vertexCount;

	auto binaryStride = getBinaryStride(attributes);
	psize binaryOffset = 0;

	for (uint32 i = 0; i < (uint32)attributes.size(); i++)
	{
		auto attributeType = attributes[i];
		auto attributeIndex = getAttributeIndex(attributeType);
		auto binarySize = toBinarySize(attributeType);
		auto destination = _destination + binaryOffset;

		if (attributeIndex >= 0 && attributeType != Attribute::Type::Tangent)
		{
			auto accessor = getAttribute(attributeIndex).getAccessor();
			auto sourceStride = accessor.getStride();
			auto sourceCount = std::min(accessor.getCount(), count);
			auto source = accessor.getBuffer() + offset * sourceStride;
			
			for (psize j = 0; j < sourceCount; j++)
			{
				memcpy(destination + j * binaryStride,
					source + j * sourceStride, binarySize);
			}

			if (attributeType == Attribute::Type::Position ||
				attributeType == Attribute::Type::Normal) // TODO: joints?
			{
				for (psize j = 0; j < sourceCount; j++)
				{
					auto value = *(const float*)(destination + j * binaryStride);
					*(float*)(destination + j * binaryStride) = -value;
				}
			}

			for (psize j = sourceCount; j < vertexCount; j++)
				memset(destination + j * binaryStride, 0, binarySize);
		}
		else
		{
			for (psize j = 0; j < count; j++)
				memset(destination + j * binaryStride, 0, binarySize);
		}

		binaryOffset += binarySize;
	}
}

//--------------------------------------------------------------------------------------------------
Model::Attribute::Type Model::Attribute::getType() const noexcept
{
	GARDEN_ASSERT(data);
	auto attribute = (cgltf_attribute*)data;
	return (Type)attribute->type;
}
Model::Accessor Model::Attribute::getAccessor() const noexcept
{
	GARDEN_ASSERT(data);
	auto attribute = (cgltf_attribute*)data;
	return Accessor(attribute->data);
}

//--------------------------------------------------------------------------------------------------
Model::Accessor::ComponentType Model::Accessor::getComponentType() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	return (ComponentType)accessor->component_type;
}
Model::Accessor::ValueType Model::Accessor::getValueType() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	return (ValueType)accessor->type;
}
Aabb Model::Accessor::getAabb() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	auto min = accessor->min, max = accessor->max;
	return Aabb(float3(-max[0], min[1], min[2]),
		float3(-min[0], max[1], max[2]));
}
bool Model::Accessor::hasAabb() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	return accessor->has_min && accessor->has_max;
}
psize Model::Accessor::getCount() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	return (psize)accessor->count;
}
psize Model::Accessor::getStride() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	return (psize)accessor->stride;
}
const uint8* Model::Accessor::getBuffer() const noexcept
{
	GARDEN_ASSERT(data);
	auto accessor = (cgltf_accessor*)data;
	GARDEN_ASSERT(!accessor->is_sparse); // TODO: support sparse.
	GARDEN_ASSERT(accessor->buffer_view);
	auto bufferView = accessor->buffer_view;

	if (bufferView->data)
		return (const uint8*)bufferView->data + accessor->offset;
	if (!bufferView->buffer->data)
		return nullptr;

	return (const uint8*)bufferView->buffer->data +
		bufferView->offset + accessor->offset;
}
psize Model::Accessor::getBinaryStride() const noexcept
{
	return toComponentCount(getValueType()) *
		toBinarySize(getComponentType());
}

//--------------------------------------------------------------------------------------------------
void Model::Accessor::copy(uint8* destination, psize count, psize offset) const noexcept
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(destination);
	GARDEN_ASSERT(count + offset < getCount() ||
		(count == 0 && offset == 0));

	auto stride = getStride();
	auto binaryStride = getBinaryStride();
	auto source = getBuffer() + offset * stride;
	if (count == 0)
		count = getCount();

	if (binaryStride == stride)
	{
		memcpy(destination, source, count * binaryStride);
	}
	else
	{
		for (psize i = 0; i < count; i++)
			memcpy(destination + i * binaryStride, source + i * stride, binaryStride);
	}
}

void Model::Accessor::copy(uint8* destination,
	ComponentType componentType, psize count, psize offset) const noexcept
{
	auto sourceComponentType = getComponentType();
	if (componentType == sourceComponentType)
	{
		copy(destination, count, offset);
		return;
	}
	
	// TODO: Take into account variable type.

	auto stride = getStride();
	auto binaryStride = getBinaryStride();
	auto source = getBuffer() + offset * stride;
	if (count == 0)
		count = getCount();

	if (sourceComponentType == ComponentType::R8U)
	{
		if (componentType == ComponentType::R16U)
		{
			auto dst = (uint16*)destination;
			if (binaryStride == stride)
			{
				auto src = (uint8*)source;
				for (psize i = 0; i < count; i++)
					dst[i] = (uint16)src[i];
			}
			else
			{
				for (psize i = 0; i < count; i++)
					dst[i] = (uint16)*((uint8*)(source + i * stride));
			}
		}
		else if (componentType == ComponentType::R32U)
		{
			auto dst = (uint32*)destination;
			if (binaryStride == stride)
			{
				auto src = (uint8*)source;
				for (psize i = 0; i < count; i++)
					dst[i] = (uint32)src[i];
			}
			else
			{
				for (psize i = 0; i < count; i++)
					dst[i] = (uint32)*((uint8*)(source + i * stride));
			}
		}
		else abort(); // TODO:
	}
	else abort(); // TODO:
}

//--------------------------------------------------------------------------------------------------
string_view Model::Material::getName() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->name ? material->name : "";
}
bool Model::Material::isUnlit() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->unlit;
}
bool Model::Material::isDoubleSided() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->double_sided;
}
Model::Material::AlphaMode Model::Material::getAlphaMode() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return (AlphaMode)material->alpha_mode;
}
float Model::Material::getAlphaCutoff() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->alpha_cutoff;
}
float3 Model::Material::getEmissiveFactor() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	auto factor = material->emissive_factor;
	auto result = float3(factor[0], factor[1], factor[2]);
	if (material->has_emissive_strength)
		result *= material->emissive_strength.emissive_strength;
	return result;
}
bool Model::Material::hasBaseColorTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->pbr_metallic_roughness.base_color_texture.texture;
}
Model::Texture Model::Material::getBaseColorTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	GARDEN_ASSERT(material->pbr_metallic_roughness.base_color_texture.texture);
	return Texture(&material->pbr_metallic_roughness.base_color_texture);
}
float4 Model::Material::getBaseColorFactor() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	auto factor = material->pbr_metallic_roughness.base_color_factor;
	return float4(factor[0], factor[1], factor[2], factor[3]);
}
bool Model::Material::hasOrmTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->pbr_metallic_roughness.metallic_roughness_texture.texture;
}
Model::Texture Model::Material::getOrmTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return Texture(&material->pbr_metallic_roughness.metallic_roughness_texture);
}
float Model::Material::getMetallicFactor() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->pbr_metallic_roughness.metallic_factor;
}
float Model::Material::getRoughnessFactor() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->pbr_metallic_roughness.roughness_factor;
}
bool Model::Material::hasNormalTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return material->normal_texture.texture;
}
Model::Texture Model::Material::getNormalTexture() const noexcept
{
	GARDEN_ASSERT(data);
	auto material = (cgltf_material*)data;
	return Texture(&material->normal_texture);
}

//--------------------------------------------------------------------------------------------------
string_view Model::Texture::getName() const noexcept
{
	GARDEN_ASSERT(data);
	auto texture = (cgltf_texture_view*)data;
	GARDEN_ASSERT(texture->texture);
	return texture->texture->name ? texture->texture->name : "";
}
string_view Model::Texture::getPath() const noexcept
{
	GARDEN_ASSERT(data);
	auto texture = (cgltf_texture_view*)data;
	GARDEN_ASSERT(texture->texture);
	GARDEN_ASSERT(texture->texture->image);
	if (!texture->texture->image->uri)
		return "";
	auto length = strlen(texture->texture->image->uri);
	if (length <= 4)
		return "";
	return string_view(texture->texture->image->uri, length - 4);
}
const uint8* Model::Texture::getBuffer() const noexcept
{
	GARDEN_ASSERT(data);
	auto texture = (cgltf_texture_view*)data;
	GARDEN_ASSERT(texture->texture);
	GARDEN_ASSERT(texture->texture->image);
	auto bufferView = texture->texture->image->buffer_view;
	if (!bufferView)
		return nullptr;
	if (bufferView->data)
		return (const uint8*)bufferView->data;
	if (!bufferView->buffer->data)
		return nullptr;
	return (const uint8*)bufferView->buffer->data + bufferView->offset;
}
psize Model::Texture::getBufferSize() const noexcept
{
	GARDEN_ASSERT(data);
	auto texture = (cgltf_texture_view*)data;
	GARDEN_ASSERT(texture->texture);
	GARDEN_ASSERT(texture->texture->image);
	auto bufferView = texture->texture->image->buffer_view;
	if (!bufferView)
		return 0;
	if (bufferView->data)
		return bufferView->size;
	if (!bufferView->buffer->data)
		return 0;
	return bufferView->buffer->size;
}

//--------------------------------------------------------------------------------------------------
static string_view cgltfToString(cgltf_result result)
{
	switch (result)
	{
		case cgltf_result_data_too_short: return "Data too short";
		case cgltf_result_unknown_format: return "Unknown format";
		case cgltf_result_invalid_json: return "Invalid JSON";
		case cgltf_result_invalid_gltf: return "Invalid glTF";
		case cgltf_result_invalid_options: return "Invalid options";
		case cgltf_result_file_not_found: return "File not found";
		case cgltf_result_io_error: return "IO error";
		case cgltf_result_out_of_memory: return "Out of memory";
		case cgltf_result_legacy_gltf: return "Legacy glTF";
		default: abort();
	}
}

//--------------------------------------------------------------------------------------------------
shared_ptr<Model> ResourceSystem::loadModel(const fs::path& path)
{
	GARDEN_ASSERT(!path.empty());
	fs::path glbPath, gltfPath;
	int32 hasGlbFile = 0, hasGltfFile = 0;
	// TODO: Use custom compressed garden model format. Load them from the pack.

	auto modelPath = fs::path("models") / path; modelPath += ".glb";
	if (getResourceFilePath(modelPath, glbPath))
		hasGlbFile = 1;
	modelPath.replace_extension(".gltf");
	if (getResourceFilePath(modelPath, gltfPath))
		hasGltfFile = 1;

	if (hasGlbFile + hasGltfFile != 1)
	{
		throw runtime_error("Model file does not exist, or it is ambiguous. ("
			"path: " + path.generic_string() + ")");
	}

	cgltf_options options;
	memset(&options, 0, sizeof(cgltf_options));
	fs::path filePath;

	if (hasGlbFile)
	{
		filePath = std::move(glbPath);
		options.type = cgltf_file_type_glb;
	}
	else if (hasGltfFile)
	{
		filePath = std::move(gltfPath);
		options.type = cgltf_file_type_gltf;
	}
	else abort();

	vector<uint8> dataBuffer;
	loadBinaryFile(filePath, dataBuffer);
	auto itemData = dataBuffer.data();
	auto itemSize = (uint32)dataBuffer.size();
	
	cgltf_data* cgltfData = nullptr;
	auto result = cgltf_parse(&options, itemData, itemSize, &cgltfData);

	if (result != cgltf_result_success)
	{
		throw runtime_error("Failed to load model. ("
			"name: " + path.generic_string() + ", "
			"error: " + string(cgltfToString(result)) + ")");
	}

	auto logSystem = getManager()->tryGet<LogSystem>();
	if (logSystem)
		logSystem->trace("Loaded model. (" + path.generic_string() + ")");

	filePath.remove_filename();
	auto relativePath = path.parent_path();

	auto model = make_shared<Model>(cgltfData,
		std::move(relativePath), std::move(filePath));
	if (hasGlbFile)
		model->data = std::move(dataBuffer);
	return model;
}
void ResourceSystem::loadModelBuffers(shared_ptr<Model> model)
{
	GARDEN_ASSERT(model);
	GARDEN_ASSERT(model->instance);
	model->buffersLocker.lock();

	if (model->isBuffersLoaded)
	{
		model->buffersLocker.unlock();
		return;
	}

	auto cgltf = (cgltf_data*)model->instance;
	cgltf_options options;
	memset(&options, 0, sizeof(cgltf_options));

	auto pathString = model->absolutePath.generic_string();
	auto result = cgltf_load_buffers(&options, cgltf, pathString.c_str());

	if (result != cgltf_result_success)
	{
		model->buffersLocker.unlock();

		throw runtime_error("Failed to load model buffers. ("
			"path: " + model->relativePath.generic_string() + ", "
			"error: " + string(cgltfToString(result)) + ")");
	}

	model->isBuffersLoaded = true;
	model->buffersLocker.unlock();
}

//--------------------------------------------------------------------------------------------------
Ref<Buffer> ResourceSystem::loadBuffer(shared_ptr<Model> model, Model::Accessor accessor,
	Buffer::Bind bind, Buffer::Access access, Buffer::Strategy strategy, bool loadAsync)
{
	GARDEN_ASSERT(model);
	GARDEN_ASSERT(hasAnyFlag(bind, Buffer::Bind::TransferDst));

	auto version = GraphicsAPI::bufferVersion++;
	auto buffer = GraphicsAPI::bufferPool.create(bind,
		access, Buffer::Usage::PreferGPU, strategy, version);
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.loaded" + to_string(*buffer));

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

//--------------------------------------------------------------------------------------------------
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
	SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer, "buffer.vertex.loaded" + to_string(*buffer));

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
#endif
*/
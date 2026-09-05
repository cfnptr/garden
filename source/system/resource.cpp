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

#include "garden/system/resource.hpp"
#include "garden/system/file-watcher.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/text.hpp"
#include "garden/system/log.hpp"
#include "garden/graphics/image-converter.hpp"
#include "garden/graphics/gsl-compiler.hpp"
#include "garden/graphics/api.hpp"
#include "garden/json-serialize.hpp"
#include "garden/profiler.hpp"
#include "garden/file.hpp"
#include "math/types.hpp"

#include "ft2build.h"
#include FT_FREETYPE_H

#include <fstream>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace garden;

//**********************************************************************************************************************
namespace garden::graphics
{
	struct ImageLoadData final
	{
		uint64 imageVersion = 0;
		vector<fs::path> paths;
		ID<Image> instance = {};
	};
	struct LodBufferLoadData final
	{
		uint64 bufferVersion = 0;
		vector<BufferChannel> channels;
		vector<fs::path> paths;
		const vector<ID<Buffer>>* vertexBuffers;
		const vector<ID<Buffer>>* indexBuffers;
		Buffer::Strategy strategy = {};
		uint8 maxLodCount = 0;
	};

	struct PipelineLoadData
	{
		uint64 pipelineVersion = 0;
		fs::path shaderPath;
		Pipeline::SpecConstValues specConstValues;
		Pipeline::SamplerStates samplerStateOverrides;
		uint32 maxBindlessCount = 0;
		#if !GARDEN_PACK_RESOURCES
		fs::path resourcesPath;
		fs::path cachePath;
		#endif
	};
	struct GraphicsPipelineLoadData final : public PipelineLoadData
	{
		vector<Image::Format> colorFormats;
		GraphicsPipeline::PipelineStates pipelineStateOverrides;
		GraphicsPipeline::BlendStates blendStateOverrides;
		ID<GraphicsPipeline> instance = {};
		Image::Format depthStencilFormat = {};
	};
	struct ComputePipelineLoadData final : public PipelineLoadData
	{
		ID<ComputePipeline> instance = {};
		bool useAsyncRecording = false;
	};
	struct RayTracingPipelineLoadData final : public PipelineLoadData
	{
		ID<RayTracingPipeline> instance = {};
		bool useAsyncRecording = false;
	};
}

const vector<string_view> ResourceSystem::imageFileExts =
{
	".gic", ".png", ".webp", ".jpg", ".jpeg", ".exr", ".hdr", ".bmp", ".psd", ".tga", ".pic", ".gif"
};
const vector<Image::FileType> ResourceSystem::imageFileTypes =
{
	Image::FileType::GIC, Image::FileType::PNG, Image::FileType::WebP, Image::FileType::JPEG, Image::FileType::JPEG, 
	Image::FileType::EXR, Image::FileType::HDR, Image::FileType::BMP, Image::FileType::PSD,  Image::FileType::TGA, 
	Image::FileType::PIC, Image::FileType::GIF
};

//**********************************************************************************************************************
ResourceSystem::ResourceSystem(bool setSingleton) : Singleton(setSingleton)
{
	static_assert(std::numeric_limits<float>::is_iec559, "Floats are not IEEE 754");

	auto manager = Manager::getInstance();
	manager->registerEvent("ImageLoaded");
	manager->registerEvent("BufferLoaded");
	ECSM_SUBSCRIBE_TO_EVENT("Init", ResourceSystem::init);

	auto appInfoSystem = AppInfoSystem::getInstance();
	appVersion = appInfoSystem->getVersion();

	#if GARDEN_PACK_RESOURCES
	try
	{
		packReader.open("resources.pack", (uint32)appVersion, true, thread::hardware_concurrency() + 1);
	}
	catch (const exception& e)
	{
		throw GardenError("Failed to open \"resources.pack\" file.");
	}
	#else
	// TODO: iterate over all .gsl files and check if any changed, then search all usages in shaders and touch depending files.
	#endif

	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	appResourcesPath = appInfoSystem->getResourcesPath();
	appCachePath = appInfoSystem->getCachePath();
	#endif
}
void ResourceSystem::init()
{
	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input);

	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	ECSM_SUBSCRIBE_TO_EVENT("FileDrop", ResourceSystem::fileDrop);
	ECSM_TRY_SUBSCRIBE_TO_EVENT("FileChange", ResourceSystem::fileChange);
	#endif
}

//**********************************************************************************************************************
void ResourceSystem::dequeuePipelines()
{
	SET_CPU_ZONE_SCOPED("Loaded Pipelines Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto graphicsPipelines = graphicsAPI->graphicsPipelinePool.getData();
	auto graphicsOccupancy = graphicsAPI->graphicsPipelinePool.getOccupancy();

	while (!loadedGraphicsQueue.empty())
	{
		auto& item = loadedGraphicsQueue.front();
		auto pipeline = *item.instance <= graphicsOccupancy ? 
			&graphicsPipelines[*item.instance - 1] : nullptr;
		
		if (!pipeline || PipelineExt::getVersion(*pipeline) != PipelineExt::getVersion(item.pipeline))
		{
			graphicsAPI->forceResourceDestroy = true;
			PipelineExt::destroy(item.pipeline);
			graphicsAPI->forceResourceDestroy = false;
			loadedGraphicsQueue.pop();
			continue;
		}
		
		GraphicsPipelineExt::moveInternalObjects(item.pipeline, *pipeline);
		loadedGraphicsQueue.pop();
		GARDEN_LOG_TRACE("Loaded graphics pipeline. (path: " + pipeline->getPath().generic_string() + ")");
	}

	auto computePipelines = graphicsAPI->computePipelinePool.getData();
	auto computeOccupancy = graphicsAPI->computePipelinePool.getOccupancy();

	while (!loadedComputeQueue.empty())
	{
		auto& item = loadedComputeQueue.front();
		auto pipeline = *item.instance <= computeOccupancy ? 
			&computePipelines[*item.instance - 1] : nullptr;

		if (!pipeline || PipelineExt::getVersion(*pipeline) != PipelineExt::getVersion(item.pipeline))
		{
			graphicsAPI->forceResourceDestroy = true;
			PipelineExt::destroy(item.pipeline);
			graphicsAPI->forceResourceDestroy = false;
			loadedComputeQueue.pop();
			continue;
		}

		ComputePipelineExt::moveInternalObjects(item.pipeline, *pipeline);
		loadedComputeQueue.pop();
		GARDEN_LOG_TRACE("Loaded compute pipeline. (path: " + pipeline->getPath().generic_string() + ")");
	}

	auto rayTracingPipelines = graphicsAPI->rayTracingPipelinePool.getData();
	auto rayTracingOccupancy = graphicsAPI->rayTracingPipelinePool.getOccupancy();

	while (!loadedRayTracingQueue.empty())
	{
		auto& item = loadedRayTracingQueue.front();
		auto pipeline = *item.instance <= rayTracingOccupancy ? 
			&rayTracingPipelines[*item.instance - 1] : nullptr;

		if (!pipeline || PipelineExt::getVersion(*pipeline) != PipelineExt::getVersion(item.pipeline))
		{
			graphicsAPI->forceResourceDestroy = true;
			PipelineExt::destroy(item.pipeline);
			graphicsAPI->forceResourceDestroy = false;
			loadedRayTracingQueue.pop();
			continue;
		}

		RayTracingPipelineExt::moveInternalObjects(item.pipeline, *pipeline);
		loadedRayTracingQueue.pop();
		GARDEN_LOG_TRACE("Loaded ray tracing pipeline. (path: " + pipeline->getPath().generic_string() + ")");
	}
}

//**********************************************************************************************************************
void ResourceSystem::dequeueImages()
{
	SET_CPU_ZONE_SCOPED("Loaded Images Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();

	#if GARDEN_DEBUG
	auto hasDequeueItems = !loadedImageQueue.empty();
	if (hasDequeueItems)
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		BEGIN_GPU_DEBUG_LABEL("Images Transfer");
		graphicsSystem->stopRecording();
	}
	#endif

	auto images = graphicsAPI->imagePool.getData();
	auto imageOccupancy = graphicsAPI->imagePool.getOccupancy();

	while (!loadedImageQueue.empty())
	{
		auto& item = loadedImageQueue.front();
		auto image = *item.instance <= imageOccupancy ? & images[*item.instance - 1] : nullptr;

		if (!image || MemoryExt::getVersion(*image) != MemoryExt::getVersion(item.image))
		{
			graphicsAPI->forceResourceDestroy = true;
			ImageExt::destroy(item.image);
			graphicsAPI->forceResourceDestroy = false;
			loadedImageQueue.pop();
			continue;
		}

		ImageExt::moveInternalObjects(item.image, *image);
		#if GARDEN_DEBUG || GARDEN_EDITOR
		image->setDebugName(image->getDebugName());
		#endif

		auto stagingBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::TransferSrc | Buffer::Usage::TransferQ, 
			Buffer::CpuAccess::SequentialWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, 0);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.loadedImage" + to_string(*stagingBuffer));

		auto stagingView = graphicsAPI->bufferPool.get(stagingBuffer);
		BufferExt::moveInternalObjects(item.staging, **stagingView);
		auto generateMipmap = image->getMipCount() > 1;
		graphicsSystem->startRecording(generateMipmap ? 
			CommandBufferType::Graphics : CommandBufferType::TransferOnly);
		Image::copy(stagingBuffer, item.instance);
		if (generateMipmap) image->generateMips();
		graphicsSystem->stopRecording();
		graphicsAPI->bufferPool.destroy(stagingBuffer);

		loadedImage = item.instance;
		loadedImagePaths = std::move(item.paths);
		manager->runEvent("ImageLoaded");

		loadedImageQueue.pop();
	}

	#if GARDEN_DEBUG
	if (hasDequeueItems)
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		END_GPU_DEBUG_LABEL();
		graphicsSystem->stopRecording();
	}
	#endif

	loadedImage = {};
	loadedImagePaths = {};
}

//**********************************************************************************************************************
void ResourceSystem::dequeueBuffers()
{
	SET_CPU_ZONE_SCOPED("Loaded Buffers Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();

	#if GARDEN_DEBUG
	auto hasDequeueItems = !loadedBufferQueue.empty();
	if (hasDequeueItems)
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		BEGIN_GPU_DEBUG_LABEL("Buffers Transfer");
		graphicsSystem->stopRecording();
	}
	#endif

	while (!loadedBufferQueue.empty())
	{
		auto& item = loadedBufferQueue.front(); // Note: getOccupancy() required, do not optimize!
		auto buffer = *item.bufferInstance <= graphicsAPI->bufferPool.getOccupancy() ? 
			&graphicsAPI->bufferPool.getData()[*item.bufferInstance - 1] : nullptr;

		if (!buffer || MemoryExt::getVersion(*buffer) != MemoryExt::getVersion(item.buffer))
		{
			graphicsAPI->forceResourceDestroy = true;
			BufferExt::destroy(item.buffer);
			graphicsAPI->forceResourceDestroy = false;
			loadedBufferQueue.pop();
			continue;
		}

		BufferExt::moveInternalObjects(item.buffer, *buffer);
		#if GARDEN_DEBUG || GARDEN_EDITOR
		buffer->setDebugName(buffer->getDebugName());
		#endif

		auto stagingBuffer = graphicsAPI->bufferPool.create(Buffer::Usage::TransferSrc,
			Buffer::CpuAccess::SequentialWrite, Buffer::Location::Auto, Buffer::Strategy::Speed, 0);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.loaded" + to_string(*stagingBuffer));

		auto stagingView = graphicsAPI->bufferPool.get(stagingBuffer);
		BufferExt::moveInternalObjects(item.staging, **stagingView);
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		Buffer::copy(stagingBuffer, item.bufferInstance);
		graphicsSystem->stopRecording();
		graphicsAPI->bufferPool.destroy(stagingBuffer);

		loadedBuffer = item.bufferInstance;
		loadedBufferPath = std::move(item.path);
		manager->runEvent("BufferLoaded");

		loadedBufferQueue.pop();
	}

	#if GARDEN_DEBUG
	if (hasDequeueItems)
	{
		graphicsSystem->startRecording(CommandBufferType::TransferOnly);
		END_GPU_DEBUG_LABEL();
		graphicsSystem->stopRecording();
	}
	#endif

	loadedBuffer = {};
	loadedBufferPath = "";
}

//**********************************************************************************************************************
void ResourceSystem::input()
{
	SET_CPU_ZONE_SCOPED("Loaded Resources Update");

	auto manager = Manager::getInstance();
	for (auto& image : loadedImageArray)
	{
		loadedImage = image.instance;
		loadedImagePaths = std::move(image.paths);
		manager->runEvent("ImageLoaded");
	}
	loadedImageArray.clear();

	for (auto& buffer : loadedBufferArray)
	{
		loadedBuffer = buffer.instance;
		loadedBufferPath = std::move(buffer.path);
		manager->runEvent("BufferLoaded");
	}
	loadedBufferArray.clear();
	
	queueLocker.lock();
	dequeuePipelines();
	dequeueImages();
	dequeueBuffers();
	queueLocker.unlock();
}

#if GARDEN_DEBUG || GARDEN_EDITOR
//**********************************************************************************************************************
static void collectChangedPipelines(const string& shaderPath, const fs::path& fileExt,
	map<fs::path, ID<GraphicsPipeline>>& graphicsPipelines,
	map<fs::path, ID<ComputePipeline>>& computePipelines,
	map<fs::path, ID<RayTracingPipeline>>& rayTracingPipelines)
{
	auto isGraphicsShader = fileExt == ".vert" || fileExt == ".frag";
	auto isComputeShader = fileExt == ".comp";
	auto isRayTracingShader = fileExt == ".rgen" || fileExt == ".rint" || fileExt == ".rahit" || 
		fileExt == ".rchit" || fileExt == ".rmiss" || fileExt == ".rcall";
	auto isMeshShader = fileExt == ".mesh" || fileExt == ".task";

	if (isGraphicsShader || isComputeShader || isRayTracingShader || isMeshShader)
	{
		auto searchResult = shaderPath.find("shaders/");
		if (searchResult == string::npos)
			return;
		searchResult += 8;

		auto resourcePath = string_view(shaderPath.c_str() + 
			searchResult, shaderPath.length() - searchResult);

		auto graphicsAPI = GraphicsAPI::get();
		if (isGraphicsShader)
		{
			auto& graphicsPipelinePool = graphicsAPI->graphicsPipelinePool;
			for (auto& pipeline : graphicsPipelinePool)
			{
				if (pipeline.getDebugName().find(resourcePath) == string::npos ||
					pipeline.getDebugName().find(".old") != string::npos) continue;
				graphicsPipelines.emplace(resourcePath, graphicsPipelinePool.getID(&pipeline));
			}
		}
		else if (isComputeShader)
		{
			auto& computePipelinePool = graphicsAPI->computePipelinePool;
			for (auto& pipeline : computePipelinePool)
			{
				if (pipeline.getDebugName().find(resourcePath) == string::npos ||
					pipeline.getDebugName().find(".old") != string::npos) continue;
				computePipelines.emplace(resourcePath, computePipelinePool.getID(&pipeline));
			}
		}
		else if (isRayTracingShader)
		{
			auto& rayTracingPipelinePool = graphicsAPI->rayTracingPipelinePool;
			for (auto& pipeline : rayTracingPipelinePool)
			{
				if (pipeline.getDebugName().find(resourcePath) == string::npos ||
					pipeline.getDebugName().find(".old") != string::npos) continue;
				rayTracingPipelines.emplace(resourcePath, rayTracingPipelinePool.getID(&pipeline));
			}
			// TODO: also detect ray tracing shader variants .1., .2. etc.
		}
		else if (isMeshShader)
		{
			abort(); // TODO: implement
		}
	}
}

//**********************************************************************************************************************
static void collectGslHeaderUsers(const fs::path& resourcesPath, const fs::path& appCachePath, 
	string_view resourcePath, vector<string>& gslHeaders, set<string>& checkedPaths,
	map<fs::path, ID<GraphicsPipeline>>& graphicsPipelines,
	map<fs::path, ID<ComputePipeline>>& computePipelines,
	map<fs::path, ID<RayTracingPipeline>>& rayTracingPipelines)
{
	for (const auto& entry : fs::recursive_directory_iterator(resourcesPath))
	{
		if (entry.is_directory())
			continue;

		auto shaderFilePath = entry.path();
		auto fileExt = shaderFilePath.extension();

		if (fileExt != ".vert" && fileExt != ".frag" && fileExt != ".comp" && fileExt != ".rgen" && 
			fileExt != ".rint" && fileExt != ".rahit" && fileExt != ".rchit" && fileExt != ".rmiss" && 
			fileExt != ".rcall" && fileExt != ".mesh" && fileExt != ".task" && fileExt != ".gsl" && fileExt != ".h")
		{
			continue;
		}

		ifstream inputStream(shaderFilePath);
		if (!inputStream.is_open())
		{
			GARDEN_LOG_ERROR("Failed to open shader file for recompile. ("
				"path: " + shaderFilePath.generic_string() + ")");
			continue;
		}

		shaderFilePath.replace_extension();
		auto shaderPath = shaderFilePath.generic_string();

		string line;
		while (getline(inputStream, line))
		{
			if (line.find("#include") == string::npos || line.find(resourcePath) == string::npos)
				continue;

			auto searchResult = shaderPath.find("shaders/");
			if (searchResult == string::npos)
				continue;
			searchResult += 8;

			auto resourcePath = string_view(shaderPath.c_str() + 
				searchResult, shaderPath.length() - searchResult);

			if (fileExt == ".gsl" || fileExt == ".h")
			{
				if (checkedPaths.emplace(string(resourcePath)).second)
					gslHeaders.push_back(string(resourcePath));
			}
			else
			{
				collectChangedPipelines(shaderPath, fileExt, graphicsPipelines, 
					computePipelines, rayTracingPipelines);

				auto path = appCachePath / "shaders" / resourcePath;
				path += fileExt; path += ".spv";

				if (fs::exists(path))
					fs::remove(path);
			}
			break;
		}
	}
}
static void collectGslHeaderUsers(const fs::path& appResourcesPath, 
	const fs::path& appCachePath, string_view resourcePath, 
	map<fs::path, ID<GraphicsPipeline>>& graphicsPipelines, 
	map<fs::path, ID<ComputePipeline>>& computePipelines, 
	map<fs::path, ID<RayTracingPipeline>>& rayTracingPipelines)
{
	try
	{
		vector<string> gslHeaders = { string(resourcePath) };
		set<string> checkedPaths; uint32 checkCount = 0;

		while (!gslHeaders.empty() && checkCount < 1000)
		{
			auto resourcePath = gslHeaders.back(); gslHeaders.pop_back();
			collectGslHeaderUsers(GARDEN_SOURCE_PATH / "shaders", appCachePath, resourcePath, 
				gslHeaders, checkedPaths, graphicsPipelines, computePipelines, rayTracingPipelines);
			collectGslHeaderUsers(appResourcesPath, appCachePath, resourcePath, gslHeaders, 
				checkedPaths, graphicsPipelines, computePipelines, rayTracingPipelines);
			checkCount++;
		}

		if (checkCount >= 1000)
			GARDEN_LOG_ERROR("Detected shader GSL circular includes!");
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to iterate shader dirs. (error: " + string(e.what()) + ")");
		return;
	}
}

//**********************************************************************************************************************
static void recompilePipelines(ResourceSystem* resourceSystem,
	const map<fs::path, ID<GraphicsPipeline>>& graphicsPipelines,
	const map<fs::path, ID<ComputePipeline>>& computePipelines,
	const map<fs::path, ID<RayTracingPipeline>>& rayTracingPipelines)
{
	auto graphicsAPI = GraphicsAPI::get();
	if (!graphicsPipelines.empty())
	{
		for (const auto& pair : graphicsPipelines)
		{
			auto& graphicsPipelinePool = graphicsAPI->graphicsPipelinePool;
			auto pipelineView = graphicsPipelinePool.get(pair.second);
			auto specConstValues = pipelineView->getSpecConstValues();

			ResourceSystem::GraphicsLoadOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.loadAsync = false;
			// TODO: store and load overrides?

			try
			{
				auto newPipeline = resourceSystem->loadGraphicsPipeline(
					pair.first, pipelineView->getFramebuffer(), &options);
				auto newPipelineView = graphicsPipelinePool.get(newPipeline);
				swap(**graphicsPipelinePool.get(pair.second), **newPipelineView);

				// TODO: we need to recreate all descriptor sets to prevent use after free errors!
				// graphicsPipelinePool.destroy(newPipeline);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (const exception& e)
			{
				GARDEN_LOG_ERROR(string(e.what()));
			}
		}
	}
	if (!computePipelines.empty())
	{
		for (const auto& pair : computePipelines)
		{
			auto& computePipelinePool = graphicsAPI->computePipelinePool;
			auto pipelineView = computePipelinePool.get(pair.second);
			auto specConstValues = pipelineView->getSpecConstValues();

			ResourceSystem::ComputeLoadOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.loadAsync = false;

			try
			{
				auto newPipeline = resourceSystem->loadComputePipeline(pair.first, &options);
				auto newPipelineView = computePipelinePool.get(newPipeline);
				swap(**computePipelinePool.get(pair.second), **newPipelineView);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (const exception& e)
			{
				GARDEN_LOG_ERROR(string(e.what()));
			}
		}
	}
	if (!rayTracingPipelines.empty())
	{
		for (const auto& pair : rayTracingPipelines)
		{
			auto& rayTracingPipelinePool = graphicsAPI->rayTracingPipelinePool;
			auto pipelineView = rayTracingPipelinePool.get(pair.second);
			auto specConstValues = pipelineView->getSpecConstValues();

			ResourceSystem::RayTracingLoadOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.loadAsync = false;

			try
			{
				auto newPipeline = resourceSystem->loadRayTracingPipeline(pair.first, &options);
				auto newPipelineView = rayTracingPipelinePool.get(newPipeline);
				swap(**rayTracingPipelinePool.get(pair.second), **newPipelineView);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (const exception& e)
			{
				GARDEN_LOG_ERROR(string(e.what()));
			}
		}
	}
}
#endif

//**********************************************************************************************************************
void ResourceSystem::fileDrop()
{
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	auto inputSystem = InputSystem::getInstance();
	auto& filePath = inputSystem->getCurrentFileDropPath();
	auto extension = filePath.extension();

	string_view resourcePath;
	if (extension == ".scene")
	{
		if (InputSystem::getResourcePath("scenes/", filePath.generic_string(), resourcePath))
			loadScene(resourcePath);
	}
	#endif
}
void ResourceSystem::fileChange()
{
	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto fileWatcherSystem = FileWatcherSystem::getInstance();
	auto shaderFilePath = fileWatcherSystem->getFilePath();
	auto fileExt = shaderFilePath.extension();
	shaderFilePath.replace_extension();
	auto shaderPath = shaderFilePath.generic_string();
	
	map<fs::path, ID<GraphicsPipeline>> graphicsPipelines;
	map<fs::path, ID<ComputePipeline>> computePipelines;
	map<fs::path, ID<RayTracingPipeline>> rayTracingPipelines;
	collectChangedPipelines(shaderPath, fileExt, graphicsPipelines, computePipelines, rayTracingPipelines);

	if (fileExt == ".gsl" || fileExt == ".h")
	{
		auto searchResult = shaderPath.find("shaders/");
		if (searchResult == string::npos)
			return;
		searchResult += 8;
	
		auto resourcePath = string_view(shaderPath.c_str() + 
			searchResult, shaderPath.length() - searchResult);
		collectGslHeaderUsers(appResourcesPath, appCachePath, resourcePath, 
			graphicsPipelines, computePipelines, rayTracingPipelines);
	}

	recompilePipelines(this, graphicsPipelines, computePipelines, rayTracingPipelines);
	#endif
}

//**********************************************************************************************************************
static void loadMissingImage(vector<uint8>& data, uint4& size, Image::Type& type, Image::Format& format) noexcept
{
	if (format == Image::Format::Undefined)
		format = Image::Format::SrgbR8G8B8A8;
	type = Image::Type::Texture2D;

	auto imageBinarySize = toBinarySize((psize)16, format);
	auto componentCount = toComponentCount(format);

	if (imageBinarySize == 0 || componentCount == 0)
	{
		data.resize(256); memset(data.data(), UINT8_MAX, 256);
		size = uint4(1);
		return;
	}

	auto compBinarySize = imageBinarySize / (componentCount * 16);
	data.resize(imageBinarySize);

	if (compBinarySize == 4)
	{
		if (componentCount == 4)
		{
			static const f32x4 colorMagenta = (f32x4)Color::magenta, colorBlack = (f32x4)Color::black;
			auto pixels = (f32x4*)data.data();
			pixels[0] = colorMagenta; pixels[1] = colorBlack;    pixels[2] = colorMagenta;  pixels[3] = colorBlack;
			pixels[4] = colorBlack;   pixels[5] = colorMagenta;  pixels[6] = colorBlack;    pixels[7] = colorMagenta;
			pixels[8] = colorMagenta; pixels[9] = colorBlack;    pixels[10] = colorMagenta; pixels[11] = colorBlack;
			pixels[12] = colorBlack;  pixels[13] = colorMagenta; pixels[14] = colorBlack;   pixels[15] = colorMagenta;
		}
		else if (componentCount == 2)
		{
			auto pixels = (float2*)data.data();
			pixels[0] = float2::one;   pixels[1] = float2::zero; pixels[2] = float2::one;   pixels[3] = float2::zero;
			pixels[4] = float2::zero;  pixels[5] = float2::one;  pixels[6] = float2::zero;  pixels[7] = float2::one;
			pixels[8] = float2::one;   pixels[9] = float2::zero; pixels[10] = float2::one;  pixels[11] = float2::zero;
			pixels[12] = float2::zero; pixels[13] = float2::one; pixels[14] = float2::zero; pixels[15] = float2::one;
		}
		else if (componentCount == 1)
		{
			auto pixels = (float*)data.data();
			pixels[0] = 1.0f;  pixels[1] = 0.0f;  pixels[2] = 1.0f;  pixels[3] = 0.0f;
			pixels[4] = 0.0f;  pixels[5] = 1.0f;  pixels[6] = 0.0f;  pixels[7] = 1.0f;
			pixels[8] = 1.0f;  pixels[9] = 0.0f;  pixels[10] = 1.0f; pixels[11] = 0.0f;
			pixels[12] = 0.0f; pixels[13] = 1.0f; pixels[14] = 0.0f; pixels[15] = 1.0f;
		}
		else abort();
	}
	else if (compBinarySize == 2)
	{
		static const half4 colorMagenta = (half4)Color::magenta, 
			colorBlack = (half4)Color::black, colorWhite = (half4)Color::white;

		if (componentCount == 4)
		{
			auto pixels = (half4*)data.data();
			pixels[0] = colorMagenta; pixels[1] = colorBlack;    pixels[2] = colorMagenta;  pixels[3] = colorBlack;
			pixels[4] = colorBlack;   pixels[5] = colorMagenta;  pixels[6] = colorBlack;    pixels[7] = colorMagenta;
			pixels[8] = colorMagenta; pixels[9] = colorBlack;    pixels[10] = colorMagenta; pixels[11] = colorBlack;
			pixels[12] = colorBlack;  pixels[13] = colorMagenta; pixels[14] = colorBlack;   pixels[15] = colorMagenta;
		}
		else if (componentCount == 2)
		{
			static const uint32 gray = *(uint32*)&colorWhite, black = *(uint32*)&colorBlack;
			auto pixels = (uint32*)data.data();
			pixels[0] = gray;   pixels[1] = black; pixels[2] = gray;   pixels[3] = black;
			pixels[4] = black;  pixels[5] = gray;  pixels[6] = black;  pixels[7] = gray;
			pixels[8] = gray;   pixels[9] = black; pixels[10] = gray;  pixels[11] = black;
			pixels[12] = black; pixels[13] = gray; pixels[14] = black; pixels[15] = gray;
		}
		else if (componentCount == 1)
		{
			static const half white = colorWhite.x, black = colorBlack.x;
			auto pixels = (half*)data.data();
			pixels[0] = white;  pixels[1] = black;  pixels[2] = white;  pixels[3] = black;
			pixels[4] = black;  pixels[5] = white;  pixels[6] = black;  pixels[7] = white;
			pixels[8] = white;  pixels[9] = black;  pixels[10] = white; pixels[11] = black;
			pixels[12] = black; pixels[13] = white; pixels[14] = black; pixels[15] = white;
		}
		else abort();
	}
	else if (compBinarySize == 1)
	{
		if (componentCount == 4)
		{
			auto pixels = (Color*)data.data();
			pixels[0] = Color::magenta;  pixels[1] = Color::black;
			pixels[2] = Color::magenta;  pixels[3] = Color::black;
			pixels[4] = Color::black;    pixels[5] = Color::magenta;
			pixels[6] = Color::black;    pixels[7] = Color::magenta;
			pixels[8] = Color::magenta;  pixels[9] = Color::black;
			pixels[10] = Color::magenta; pixels[11] = Color::black;
			pixels[12] = Color::black;   pixels[13] = Color::magenta;
			pixels[14] = Color::black;   pixels[15] = Color::magenta;
		}
		else if (componentCount == 2)
		{
			auto pixels = (uint16*)data.data();
			pixels[0] = UINT16_MAX; pixels[1] = 0;           pixels[2] = UINT16_MAX;  pixels[3] = 0;
			pixels[4] = 0;          pixels[5] = UINT16_MAX;  pixels[6] = 0;           pixels[7] = UINT16_MAX;
			pixels[8] = UINT16_MAX; pixels[9] = 0;           pixels[10] = UINT16_MAX; pixels[11] = 0;
			pixels[12] = 0;         pixels[13] = UINT16_MAX; pixels[14] = 0;          pixels[15] = UINT16_MAX;
		}
		else if (componentCount == 1)
		{
			auto pixels = (uint8*)data.data();
			pixels[0] = UINT8_MAX; pixels[1] = 0;          pixels[2] = UINT8_MAX;  pixels[3] = 0;
			pixels[4] = 0;         pixels[5] = UINT8_MAX;  pixels[6] = 0;          pixels[7] = UINT8_MAX;
			pixels[8] = UINT8_MAX; pixels[9] = 0;          pixels[10] = UINT8_MAX; pixels[11] = 0;
			pixels[12] = 0;        pixels[13] = UINT8_MAX; pixels[14] = 0;         pixels[15] = UINT8_MAX;
		}
		else abort();
	}
	else memset(data.data(), UINT8_MAX, data.size());

	size = uint4(4, 4, 1, 1);
}
static void loadMissingImage(vector<uint8>& nx, vector<uint8>& px, vector<uint8>& ny, vector<uint8>& py, 
	vector<uint8>& nz, vector<uint8>& pz, uint2& size, Image::Format& format) noexcept
{
	uint4 missingSize; Image::Type missingType;
	loadMissingImage(nx, missingSize, missingType, format);
	loadMissingImage(px, missingSize, missingType, format);
	loadMissingImage(ny, missingSize, missingType, format);
	loadMissingImage(py, missingSize, missingType, format);
	loadMissingImage(nz, missingSize, missingType, format);
	loadMissingImage(pz, missingSize, missingType, format);
	size = (uint2)missingSize;
}

//**********************************************************************************************************************
static void loadMissingModel(const vector<BufferChannel>& channels, vector<uint8>& vertexData, 
	vector<uint8>& indexData, uint32& vertexCount, uint32& indexCount)
{
	const float4 colorMagenta = (float4)Color::magenta; const float4 colorBlack = (float4)Color::black;
	auto vertexBinarySize = toBinarySize(channels);
	vertexData.resize(vertexBinarySize * 3);
	auto vertices = vertexData.data();

	for (auto channel : channels)
	{
		switch (channel)
		{
		case BufferChannel::Positions:
			*(float3*)(vertices                       ) = float3(-1.0f, -1.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize    ) = float3( 1.0f, -1.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize * 2) = float3( 1.0f,  1.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Normals:
			*(float3*)(vertices                       ) = float3(0.0f, 0.0f, -1.0f);
			*(float3*)(vertices + vertexBinarySize    ) = float3(0.0f, 0.0f, -1.0f);
			*(float3*)(vertices + vertexBinarySize * 2) = float3(0.0f, 0.0f, -1.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Tangents:
			*(float3*)(vertices                       ) = float3(1.0f, 0.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize    ) = float3(1.0f, 0.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize * 2) = float3(1.0f, 0.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Bitangents:
			*(float3*)(vertices                       ) = float3(0.0f, 1.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize    ) = float3(0.0f, 1.0f, 0.0f);
			*(float3*)(vertices + vertexBinarySize * 2) = float3(0.0f, 1.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::TextureCoords:
			*(float2*)(vertices                       ) = float2(0.0f, 0.0f);
			*(float2*)(vertices + vertexBinarySize    ) = float2(1.0f, 0.0f);
			*(float2*)(vertices + vertexBinarySize * 2) = float2(0.5f, 1.0f);
			vertices += sizeof(float2);
			break;
		case BufferChannel::VertexColors:
			*(float4*)(vertices                       ) = colorBlack;
			*(float4*)(vertices + vertexBinarySize    ) = colorBlack;
			*(float4*)(vertices + vertexBinarySize * 2) = colorMagenta;
			vertices += sizeof(float4);
			break;
		default: abort();
		}
	}
}

#if !GARDEN_PACK_RESOURCES
//**********************************************************************************************************************
static int32 getImageFilePath(const fs::path& appResourcesPath,
	fs::path imagePath, fs::path& filePath, Image::FileType& fileType)
{
	int32 fileCount = 0;
	for (uint8 i = 0; i < (uint8)ResourceSystem::imageFileExts.size(); i++)
	{
		imagePath.replace_extension(ResourceSystem::imageFileExts[i]);
		if (File::tryGetResourcePath(appResourcesPath, imagePath, filePath))
		{
			fileType = ResourceSystem::imageFileTypes[i];
			fileCount++;
		}
	}

	return fileCount;
}
static int32 getImageFilePath(const fs::path& appCachePath, const fs::path& appResourcesPath,
	const fs::path& imagePath, fs::path& filePath, Image::FileType& fileType)
{
	auto cacheImagePath = appCachePath / fs::path("images") / imagePath;
	cacheImagePath.replace_extension(".gic");
	if (fs::exists(cacheImagePath))
	{
		filePath = std::move(cacheImagePath);
		fileType = Image::FileType::GIC;
		return 1;
	}

	auto fileCount = getImageFilePath(appResourcesPath, fs::path("images") / imagePath, filePath, fileType);
	fileCount += getImageFilePath(appResourcesPath, fs::path("models") / imagePath, filePath, fileType);
	return fileCount;
}
#endif

//**********************************************************************************************************************
bool ResourceSystem::loadImageData(const fs::path& path, vector<uint8>& pixels, 
	uint4& size, Image::Type& type, Image::Format& format, int32 threadIndex) const noexcept
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());
	vector<uint8> imageData; Image::FileType fileType;

	#if GARDEN_PACK_RESOURCES
	if (threadIndex < 0)
		threadIndex = 0;
	else threadIndex++;

	auto imagePath = fs::path("images") / path;
	uint64 itemIndex = 0;

	for (uint8 i = 0; i < (uint8)imageFileExts.size(); i++)
	{
		imagePath.replace_extension(imageFileExts[i]);
		if (packReader.getItemIndex(imagePath, itemIndex))
			fileType = imageFileTypes[i];
	}

	if (fileType == Image::FileType::Count)
	{
		GARDEN_LOG_ERROR("Image does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}

	packReader.readItemData(itemIndex, imageData, threadIndex);
	#else
	fs::path filePath;
	auto fileCount = getImageFilePath(appCachePath, appResourcesPath, path, filePath, fileType);

	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("Image file does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("Image file is ambiguous. (path: " + path.generic_string() + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}

	try
	{
		File::loadBinary(filePath, imageData);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR(string(e.what()));
		loadMissingImage(pixels, size, type, format);
		return false;
	}
	#endif

	try
	{
		Image::loadFileData(imageData.data(), imageData.size(), fileType, pixels, size, type, format);
		GARDEN_LOG_TRACE("Loaded image. (path: " + path.generic_string() + ")");
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load image data. (path: " + 
			path.generic_string() + ", error: " + string(e.what()) + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}

	#if !GARDEN_PACK_RESOURCES
	auto metadataPath = path; metadataPath.replace_extension(".meta");
	float effort = 0.7f; auto storeFlags = Image::StoreFlag::None;

	if (fs::exists(metadataPath))
	{
		try
		{
			Image::loadFileMetadata(metadataPath, pixels, size, type, format, effort, storeFlags);
		}
		catch (const exception& e)
		{
			GARDEN_LOG_ERROR("Failed to load image metadata. (path: " + 
				path.generic_string() + ", error: " + string(e.what()) + ")");
		}
	}
	#endif

	return true;
}

//**********************************************************************************************************************
bool ResourceSystem::loadOrConvertCubemap(const fs::path& path, vector<uint8>& nx, 
	vector<uint8>& px, vector<uint8>& ny, vector<uint8>& py, vector<uint8>& nz, 
	vector<uint8>& pz, uint2& size, Image::Format& format, int32 threadIndex) const noexcept
{
	#if !GARDEN_PACK_RESOURCES
	fs::path inputFilePath; Image::FileType inputFileType; 
	auto fileCount = getImageFilePath(appCachePath, appResourcesPath, path, inputFilePath, inputFileType);

	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("Cubemap image file does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(nx, px, ny, py, nz, pz, size, format);
		return false;
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("Cubemap image file is ambiguous. (path: " + path.generic_string() + ")");
		loadMissingImage(nx, px, ny, py, nz, pz, size, format);
		return false;
	}
	
	auto cacheFilePath = appCachePath / "images" / path;
	auto cacheFileString = cacheFilePath.generic_string();
	auto cacheFileNX = cacheFileString + "-nx.gic", cacheFilePX = cacheFileString + "-px.gic";
	auto cacheFileNY = cacheFileString + "-ny.gic", cacheFilePY = cacheFileString + "-py.gic";
	auto cacheFileNZ = cacheFileString + "-nz.gic", cacheFilePZ = cacheFileString + "-pz.gic";
	auto lastWriteTime = fs::last_write_time(inputFilePath);

	if (!fs::exists(cacheFileNX) || !fs::exists(cacheFilePX) || !fs::exists(cacheFileNY) || 
		!fs::exists(cacheFilePY) || !fs::exists(cacheFileNZ) || !fs::exists(cacheFilePZ) ||
		lastWriteTime > fs::last_write_time(cacheFileNX) || lastWriteTime > fs::last_write_time(cacheFilePX) ||
		lastWriteTime > fs::last_write_time(cacheFileNY) || lastWriteTime > fs::last_write_time(cacheFilePY) ||
		lastWriteTime > fs::last_write_time(cacheFileNZ) || lastWriteTime > fs::last_write_time(cacheFilePZ))
	{
		auto threadSystem = ThreadSystem::tryGetInstance();
		auto threadPool = threadIndex < 0 && threadSystem ? &threadSystem->getForegroundPool() : nullptr;

		auto equi2cubeResult = false;
		try
		{
			equi2cubeResult = ImageConverter::equi2cube(inputFilePath.filename(), 
				inputFilePath.parent_path(), cacheFilePath.parent_path(), threadPool);
			GARDEN_LOG_DEBUG("Converted cubemap. (path: " + path.generic_string() + ")");
		}
		catch (const exception& e)
		{
			GARDEN_LOG_ERROR("Failed to convert cubemap image. (path: " + 
				path.generic_string() + ", error: " + string(e.what()) + ")");
			loadMissingImage(nx, px, ny, py, nz, pz, size, format);

			fs::remove(cacheFileNX); fs::remove(cacheFilePX);
			fs::remove(cacheFileNY); fs::remove(cacheFilePY);
			fs::remove(cacheFileNZ); fs::remove(cacheFilePZ);
			return false;
		}

		if (!equi2cubeResult)
			GARDEN_LOG_ERROR("Cubemap file does not exist. (path: " + path.generic_string() + ")");
	}
	#endif

	return loadCubemapData(path, nx, px, ny, py, nz, pz, size, format);
}

//**********************************************************************************************************************
bool ResourceSystem::loadOrConvertImage(const fs::path& path, vector<uint8>& pixels, 
	uint4& size, Image::Type& type, Image::Format& format, int32 threadIndex) const noexcept
{
	#if !GARDEN_PACK_RESOURCES
	fs::path inputFilePath; Image::FileType inputFileType; 
	auto fileCount = getImageFilePath(appCachePath, appResourcesPath, path, inputFilePath, inputFileType);

	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("Image file does not exist. (path: " + path.generic_string() + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("Image file is ambiguous. (path: " + path.generic_string() + ")");
		loadMissingImage(pixels, size, type, format);
		return false;
	}

	auto cacheFilePath = appCachePath / "images" / path;
	auto cacheGicPath = cacheFilePath.generic_string() + ".gic";

	if (!fs::exists(cacheGicPath) || fs::last_write_time(inputFilePath) > fs::last_write_time(cacheGicPath))
	{
		auto compressResult = false;
		try
		{
			compressResult = ImageConverter::compress(inputFilePath.filename(), 
				inputFilePath.parent_path(), cacheFilePath.parent_path());
			GARDEN_LOG_DEBUG("Converted image. (path: " + path.generic_string() + ")");
		}
		catch (const exception& e)
		{
			GARDEN_LOG_ERROR("Failed to compress image. (path: " + 
				path.generic_string() + ", error: " + string(e.what()) + ")");
			loadMissingImage(pixels, size, type, format);
			fs::remove(cacheGicPath);
			return false;
		}

		if (!compressResult)
			GARDEN_LOG_ERROR("Image file does not exist. (path: " + path.generic_string() + ")");
	}
	#endif

	return loadImageData(path, pixels, size, type, format, threadIndex);
}

//**********************************************************************************************************************
bool ResourceSystem::loadImageData(const fs::path* paths, psize pathCount, vector<vector<uint8>>& pixelArrays, 
	uint4& size, Image::Type& type, Image::Format& format, int32 threadIndex) const noexcept
{
	GARDEN_ASSERT(paths);
	GARDEN_ASSERT(pathCount > 0);
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	auto pixelArrayData = pixelArrays.data();
	auto result = loadOrConvertImage(paths[0], pixelArrayData[0], size, type, format, threadIndex);

	for (psize i = 1; i < pathCount; i++)
	{
		uint4 elementSize;
		result &= loadOrConvertImage(paths[i], pixelArrayData[i], elementSize, type, format, threadIndex);

		if (size != elementSize)
		{
			auto pixelCount = (psize)size.x * size.y;
			auto imageBinarySize = toBinarySize(pixelCount, format);
			GARDEN_ASSERT_MSG(imageBinarySize > 0, "Assert " + paths[i].generic_string());

			auto& pixelArray = pixelArrayData[i];
			pixelArray.resize(imageBinarySize);

			if (imageBinarySize / pixelCount == 4)
			{
				auto pixels = (Color*)pixelArray.data();
				for (uint32 j = 0; j < pixelCount; j++)
					pixels[j] = Color::magenta;
			}
			else if (format == Image::Format::SfloatR16G16B16A16)
			{
				auto pixels = (half4*)pixelArray.data();
				for (uint32 j = 0; j < pixelCount; j++)
					pixels[j] = half4(1.0f, 0.0f, 1.0f, 1.0f); 
			}
			else if (format == Image::Format::SfloatR32G32B32A32)
			{
				auto pixels = (f32x4*)pixelArray.data();
				for (uint32 j = 0; j < pixelCount; j++)
					pixels[j] = f32x4(1.0f, 0.0f, 1.0f, 1.0f); 
			}
			else memset(pixelArray.data(), UINT8_MAX, pixelArray.size());

			GARDEN_LOG_ERROR("Different image array element size. (path: " + paths[i].generic_string() + ")");
			result = false;
		}
	}
	return result;
}

//**********************************************************************************************************************
bool ResourceSystem::loadCubemapData(const fs::path& path, vector<uint8>& nx, 
	vector<uint8>& px, vector<uint8>& ny, vector<uint8>& py, vector<uint8>& nz, 
	vector<uint8>& pz, uint2& size, Image::Format& format, int32 threadIndex) const noexcept
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	auto threadSystem = ThreadSystem::tryGetInstance();
	uint4 nxSize, pxSize, nySize, pySize, nzSize, pzSize;
	Image::Type nxType, pxType, nyType, pyType, nzType, pzType;
	Image::Format nxFormat, pxFormat, nyFormat, pyFormat, nzFormat, pzFormat; bool result;

	if (threadIndex < 0 && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		atomic_uint8_t loadResult = 0;

		threadPool.addTasks([&](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Cubemap Data Load");

			auto filePath = path.generic_string();
			auto threadIndex = task.getThreadIndex();

			switch (task.getTaskIndex())
			{
				case 0: loadResult += loadImageData(filePath + "-nx", 
					nx, nxSize, nxType, nxFormat, threadIndex) ? 1 : 0; break;
				case 1: loadResult += loadImageData(filePath + "-px", 
					px, pxSize, pxType, pxFormat, threadIndex) ? 1 : 0; break;
				case 2: loadResult += loadImageData(filePath + "-ny", 
					ny, nySize, nyType, nyFormat, threadIndex) ? 1 : 0; break;
				case 3: loadResult += loadImageData(filePath + "-py", 
					py, pySize, pyType, pyFormat, threadIndex) ? 1 : 0; break;
				case 4: loadResult += loadImageData(filePath + "-nz", 
					nz, nzSize, nzType, nzFormat, threadIndex) ? 1 : 0; break;
				case 5: loadResult += loadImageData(filePath + "-pz", 
					pz, pzSize, pzType, pzFormat, threadIndex) ? 1 : 0; break;
				default: abort();
			}
		}, Image::cubemapFaceCount);
		threadPool.wait();

		result = loadResult == 6;
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Cubemap Data Load");

		auto filePath = path.generic_string();
		result = loadImageData(filePath + "-nx", nx, nxSize, nxType, nxFormat, threadIndex);
		result &= loadImageData(filePath + "-px", px, pxSize, pxType, pxFormat, threadIndex);
		result &= loadImageData(filePath + "-ny", ny, nySize, nyType, nyFormat, threadIndex);
		result &= loadImageData(filePath + "-py", py, pySize, pyType, pyFormat, threadIndex);
		result &= loadImageData(filePath + "-nz", nz, nzSize, nzType, nzFormat, threadIndex);
		result &= loadImageData(filePath + "-pz", pz, pzSize, pzType, pzFormat, threadIndex);
	}

	if (nxSize.z != 1 || nxSize != pxSize || nxSize != nySize || 
		nxSize != pySize || nxSize != nzSize || nxSize != pzSize ||
		nxSize.x % 32 != 0 || pxSize.x % 32 != 0 || nySize.x % 32 != 0 ||
		pySize.x % 32 != 0 || nzSize.x % 32 != 0 || pzSize.x % 32 != 0)
	{
		GARDEN_LOG_ERROR("Invalid cubemap size. (path: " + path.generic_string() + ")");
		loadMissingImage(nx, px, ny, py, nz, pz, size, format);
		return false;
	}
	if (nxSize.x != nxSize.y || pxSize.x != pxSize.y || nySize.x != nySize.y || 
		pySize.x != pySize.y || nzSize.x != nzSize.y || pzSize.x != pzSize.y)
	{
		GARDEN_LOG_ERROR("Invalid cubemap face size. (path: " + path.generic_string() + ")");
		loadMissingImage(nx, px, ny, py, nz, pz, size, format);
		return false;
	}
	if (nxFormat != pxFormat || nxFormat != nyFormat || 
		nxFormat != pyFormat || nxFormat != nzFormat || nxFormat != pzFormat)
	{
		GARDEN_LOG_ERROR("Invalid cubemap face format. (path: " + path.generic_string() + ")");
		loadMissingImage(nx, px, ny, py, nz, pz, size, format);
		return false;
	}

	format = nxFormat;
	size = (uint2)nxSize;
	return result;
}

//**********************************************************************************************************************
static constexpr Image::Usage loadedImageUsage = Image::Usage::Sampled | Image::Usage::TransferDst;
static constexpr Image::Strategy loadedImageStrategy = Image::Strategy::Size;

static void copyLoadedImageData(const vector<vector<uint8>>& pixelArrays, 
	uint8* stagingMap, const uint8* stagingMapEnd, Image::Type imageType) noexcept
{
	auto pixelData = pixelArrays.data();
	auto layerCount = (uint32)pixelArrays.size();

	for (uint32 layer = 0; layer < layerCount; layer++)
	{
		auto& pixels = pixelData[Image::calcApiLayerIndex(imageType, layer)];
		GARDEN_ASSERT(stagingMap < stagingMapEnd);
		memcpy(stagingMap, pixels.data(), pixels.size());
		stagingMap += pixels.size();
	}
	GARDEN_ASSERT(stagingMap == stagingMapEnd);
}
ID<Image> ResourceSystem::loadImage(const fs::path* paths, psize pathCount, bool loadAsync)
{
	GARDEN_ASSERT(paths);
	GARDEN_ASSERT(pathCount > 0);
	
	auto graphicsAPI = GraphicsAPI::get();
	auto imageVersion = graphicsAPI->imageVersion++;

	auto image = graphicsAPI->imagePool.create(loadedImageUsage | 
		Image::Usage::TransferQ, loadedImageStrategy, imageVersion);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto imageView = graphicsAPI->imagePool.get(image);
	imageView->setDebugName("image." + paths[0].generic_string());
	#endif

	auto threadSystem = ThreadSystem::tryGetInstance();
	if (loadAsync && threadSystem)
	{
		auto data = new ImageLoadData();
		data->imageVersion = imageVersion;
		data->paths.assign(paths, paths + pathCount);
		data->instance = image;

		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Image Load");

			auto& filePaths = data->paths; Image::Type type; Image::Format format;
			vector<vector<uint8>> pixelArrays(filePaths.size()); uint4 size;
	
			auto p = filePaths[0].generic_string();
			if (p.find("cubemap") != string::npos)
			{
				pixelArrays.resize(Image::cubemapFaceCount); uint2 cubeSize;
				loadOrConvertCubemap(filePaths[0], pixelArrays[0], pixelArrays[1], pixelArrays[2], pixelArrays[3], 
					pixelArrays[4], pixelArrays[5], cubeSize, format, task.getThreadIndex());
				filePaths = { p + "-nx", p + "-px", p + "-ny", p + "-py", p + "-nz", p + "-pz" };
				size = uint4(cubeSize.x, cubeSize.y, 1, 1); type = Image::Type::Cubemap;
			}
			else
			{
				loadImageData(filePaths.data(), filePaths.size(), // Note: loads or converts inside.
					pixelArrays, size, type, format, task.getThreadIndex());
			}

			auto pixelCount = (psize)size.x * size.y * size.z;
			auto imageBinarySize = toBinarySize(pixelCount, format);
			GARDEN_ASSERT_MSG(imageBinarySize > 0, "Assert " + filePaths[0].generic_string());

			ImageQueueItem item =
			{
				ImageExt::create(type, format, loadedImageUsage, loadedImageStrategy, (u32x4)size, data->imageVersion),
				BufferExt::create(Buffer::Usage::TransferSrc, Buffer::CpuAccess::SequentialWrite, 
					Buffer::Location::Auto, Buffer::Strategy::Speed, imageBinarySize * filePaths.size(), 0),
				std::move(filePaths), data->instance // Note: Staging does not need TransferQ flag.
			};

			auto stagingMap = item.staging.getMap();
			copyLoadedImageData(pixelArrays, stagingMap, 
				stagingMap + item.staging.getBinarySize(), type);
			item.staging.flush();

			queueLocker.lock();
			loadedImageQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		TaskPriority::image);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Image Load");

		LoadedImageItem item; Image::Type type; Image::Format format;
		vector<vector<uint8>> pixelArrays(pathCount); uint4 size;

		auto p = paths[0].generic_string();
		if (p.find("cubemap") != string::npos)
		{
			pixelArrays.resize(Image::cubemapFaceCount); uint2 cubeSize;
			loadOrConvertCubemap(paths[0], pixelArrays[0], pixelArrays[1], pixelArrays[2], 
				pixelArrays[3], pixelArrays[4], pixelArrays[5], cubeSize, format, -1);
			item.paths = { p + "-nx", p + "-px", p + "-ny", p + "-py", p + "-nz", p + "-pz" };
			size = uint4(cubeSize.x, cubeSize.y, 1, 1); type = Image::Type::Cubemap;
		}
		else
		{
			// Note: loads or converts inside.
			loadImageData(paths, pathCount, pixelArrays, size, type, format, -1);
			item.paths.assign(paths, paths + pathCount);
		}

		auto pixelCount = (psize)size.x * size.y * size.z;
		auto imageBinarySize = toBinarySize(pixelCount, format);
		GARDEN_ASSERT_MSG(imageBinarySize > 0, "Assert " + paths[0].generic_string());

		auto imageInstance = ImageExt::create(type, format, 
			loadedImageUsage, loadedImageStrategy, (u32x4)size, 0);
		auto imageView = graphicsAPI->imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto graphicsSystem = GraphicsSystem::getInstance();
		auto stagingBuffer =  graphicsSystem->createStagingBuffer(
			Buffer::CpuAccess::SequentialWrite, imageBinarySize * pathCount);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.loadedImage" + to_string(*stagingBuffer));

		auto stagingView = graphicsAPI->bufferPool.get(stagingBuffer);
		auto stagingMap = stagingView->getMap();
		copyLoadedImageData(pixelArrays, stagingMap, 
			stagingMap + stagingView->getBinarySize(), type);
		stagingView->flush();

		auto generateMipmap = imageView->getMipCount() > 1;
		graphicsSystem->startRecording(generateMipmap ? 
			CommandBufferType::Graphics : CommandBufferType::TransferOnly);
		Image::copy(stagingBuffer, image);
		if (generateMipmap) imageView->generateMips();
		graphicsSystem->stopRecording();
		graphicsAPI->bufferPool.destroy(stagingBuffer);

		item.instance = image;
		loadedImageArray.push_back(std::move(item));
	}
	return image;
}

//**********************************************************************************************************************
Ref<Image> ResourceSystem::loadSharedImage(const fs::path* paths, psize pathCount, bool loadAsync)
{
	GARDEN_ASSERT(paths);
	GARDEN_ASSERT(pathCount > 0);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	for (psize i = 0; i < pathCount; i++)
	{
		auto path = paths[i].generic_string();
		Hash128::updateState(hashState, path.c_str(), path.length());
	}
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedImages.find(hash);
	if (searchResult != sharedImages.end())
	{
		auto imageView = GraphicsSystem::getInstance()->get(searchResult->second);
		if (imageView->isLoaded())
		{
			LoadedImageItem item;
			item.paths.assign(paths, paths + pathCount);
			item.instance = ID<Image>(searchResult->second);
			loadedImageArray.push_back(std::move(item));
		}
		return searchResult->second;
	}

	auto image = Ref<Image>(loadImage(paths, pathCount, loadAsync)); 
	auto result = sharedImages.emplace(hash, image);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	return image;
}
void ResourceSystem::destroyShared(Ref<Image>& image)
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

	GraphicsSystem::getInstance()->destroy(image);
}

//**********************************************************************************************************************
void ResourceSystem::storeImage(const fs::path& path, const void* pixels, uint3 size, Image::FileType fileType, 
	Image::Type imageType, Image::Format imageFormat, float quality, float effort, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(pixels, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(areAllTrue(size > uint3::zero), "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(imageFormat != Image::Format::Undefined, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(quality >= 0.0f && quality <= 1.0f, "Assert " + path.generic_string());
	GARDEN_ASSERT_MSG(effort >= 0.0f && effort <= 1.0f, "Assert " + path.generic_string());

	#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
	auto imagesPath = directory.empty() ? appResourcesPath / "images" : directory;
	#else
	const auto& imagesPath = directory;
	#endif

	auto directoryPath = imagesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	Image::storeFileData(imagesPath / path, pixels, size, fileType, imageType, imageFormat, quality, effort);
	GARDEN_LOG_DEBUG("Stored image. (path: " + path.generic_string() + ")");
}

void ResourceSystem::combineImages(const vector<fs::path>& inputPaths, const fs::path& outputPath, 
	Image::FileType fileType, Image::Format imageFormat, float quality, float effort, int32 threadIndex)
{
	GARDEN_ASSERT(!inputPaths.empty());
	GARDEN_ASSERT(!inputPaths[0].empty());
	GARDEN_ASSERT(!outputPath.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	vector<uint8> inBuffer; uint4 inSize; Image::Type inType; Image::Format inFormat;
	loadImageData(inputPaths[0], inBuffer, inSize, inType, inFormat, threadIndex);

	if (inSize.z != 1 || inType != Image::Type::Texture2D)
	{
		throw GardenError("Failed to combine images, image is not 2D. ("
			"path: " + inputPaths[0].generic_string() + ")");
	}

	auto pathCount = (uint32)inputPaths.size();
	auto pixelCount = (psize)inSize.x * inSize.y;
	auto imageBinarySize = toBinarySize(pixelCount, imageFormat);
	GARDEN_ASSERT(imageBinarySize > 0);

	auto pixelBinarySize = imageBinarySize / pixelCount;
	auto inBinSizeX = pixelBinarySize * inSize.x, inBinSizeY = pixelBinarySize * inSize.y;
	auto outBinSizeY = pixelBinarySize * inSize.y * pathCount;
	vector<uint8> outBuffer(inSize.x * outBinSizeY), tmpBuffer;

	auto tmpData = (const uint8*)Image::convertFormat(inBuffer.data(), 
		uint3((uint2)inSize, 1), tmpBuffer, inFormat, imageFormat);
	auto outData = outBuffer.data();

	for (uint32 y = 0; y < inSize.y; y++)
	{
		memcpy(outData, tmpData, inBinSizeX);
		tmpData += inBinSizeY; outData += outBinSizeY;
	}

	auto inputPathData = inputPaths.data();
	for (uint32 i = 1; i < pathCount; i++)
	{
		uint4 otherSize; Image::Type otherType; Image::Format otherFormat;
		loadImageData(inputPathData[i], inBuffer, otherSize, otherType, otherFormat, threadIndex);

		if (inSize != otherSize || inType != otherType)
		{
			throw GardenError("Failed to combine images, different sizes or types. ("
				"path: " + inputPaths[0].generic_string() + ")");
		}

		tmpData = (const uint8*)Image::convertFormat(inBuffer.data(), 
			uint3((uint2)inSize, 1), tmpBuffer, inFormat, imageFormat);
		outData = outBuffer.data() + inBinSizeX * i;

		for (uint32 y = 0; y < inSize.y; y++)
		{
			memcpy(outData, tmpData, inBinSizeX);
			tmpData += inBinSizeY; outData += outBinSizeY;
		}
	}

    auto outSize = uint2(inSize.x * (uint32)inputPaths.size(), inSize.y);
	storeImage(outputPath, outBuffer.data(), uint3(outSize, 1), 
		fileType, Image::Type::Texture2D, imageFormat, quality, effort);
}

//**********************************************************************************************************************
void ResourceSystem::renormalizeImage(const fs::path& path, 
	Image::FileType fileType, float quality, float effort, int32 threadIndex)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(threadIndex < (int32)thread::hardware_concurrency());

	vector<uint8> dataBuffer; uint4 size; Image::Type imageType; Image::Format imageFormat;
	loadImageData(path, dataBuffer, size, imageType, imageFormat, threadIndex);

	auto pixelCount = (psize)size.x * size.y * size.z;
	if (imageFormat == Image::Format::SrgbR8G8B8A8 || imageFormat == Image::Format::UnormR8G8B8A8)
	{
		auto pixelData = (Color*)dataBuffer.data();
		for (psize i = 0; i < 0; i++)
			pixelData[i] = (Color)normalize3((f32x4)pixelData[i]);
	}
	else if (imageFormat == Image::Format::SfloatR32G32B32A32)
	{
		auto pixelData = (f32x4*)dataBuffer.data();
		for (psize i = 0; i < 0; i++)
			pixelData[i] = normalize3(pixelData[i]);
	}
	else if (imageFormat == Image::Format::SfloatR16G16B16A16)
	{
		auto pixelData = (half4*)dataBuffer.data();
		for (psize i = 0; i < 0; i++)
			pixelData[i] = (half4)normalize3((f32x4)pixelData[i]);
	}
	else
	{
		throw GardenError("Unsupported normal map image format. ("
			"path: " + path.generic_string() + ")");
	}
	
	storeImage(path, dataBuffer.data(), (uint3)size, fileType, imageType, imageFormat);
}

//**********************************************************************************************************************
ID<Buffer> ResourceSystem::loadBuffer(const fs::path& path, float taskPriority, bool loadAsync)
{
	GARDEN_ASSERT(!path.empty());
	abort(); // TODO: load plain buffer binary data.
}
Ref<Buffer> ResourceSystem::loadSharedBuffer(const fs::path& path, float taskPriority, bool loadAsync)
{
	GARDEN_ASSERT(!path.empty());

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	auto pathString = path.generic_string();
	Hash128::updateState(hashState, pathString.c_str(), pathString.length());
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedBuffers.find(hash);
	if (searchResult != sharedBuffers.end())
	{
		auto bufferView = GraphicsSystem::getInstance()->get(searchResult->second);
		if (bufferView->isLoaded())
		{
			LoadedBufferItem item;
			item.path = path;
			item.instance = ID<Buffer>(searchResult->second);
			loadedBufferArray.push_back(std::move(item));
		}
		return searchResult->second;
	}

	auto buffer = Ref<Buffer>(loadBuffer(path, taskPriority, loadAsync)); 
	auto result = sharedBuffers.emplace(hash, buffer);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	return buffer;
}

void ResourceSystem::destroyShared(Ref<Buffer>& buffer)
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

	GraphicsSystem::getInstance()->destroy(buffer);
}

//**********************************************************************************************************************
Ref<DescriptorSet> ResourceSystem::createSharedDS(const Hash128& hash, 
	ID<GraphicsPipeline> graphicsPipeline, DescriptorSet::Uniforms&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(graphicsPipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchResult = sharedDescriptorSets.find(hash);
	if (searchResult != sharedDescriptorSets.end())
		return searchResult->second;

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto descriptorSet = graphicsSystem->createDescriptorSet(graphicsPipeline, std::move(uniforms), {}, index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64URL());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	return sharedDescriptorSet;
}
Ref<DescriptorSet> ResourceSystem::createSharedDS(const Hash128& hash, 
	ID<ComputePipeline> computePipeline, DescriptorSet::Uniforms&& uniforms, uint8 index)
{
	GARDEN_ASSERT(hash);
	GARDEN_ASSERT(computePipeline);
	GARDEN_ASSERT(!uniforms.empty());

	auto searchResult = sharedDescriptorSets.find(hash);
	if (searchResult != sharedDescriptorSets.end())
		return searchResult->second;

	auto graphicsSystem = GraphicsSystem::getInstance();
	auto descriptorSet = graphicsSystem->createDescriptorSet(computePipeline, std::move(uniforms), {}, index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64URL());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	return sharedDescriptorSet;
}

void ResourceSystem::destroyShared(Ref<DescriptorSet>& descriptorSet)
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

	GraphicsSystem::getInstance()->destroy(descriptorSet);
}

//**********************************************************************************************************************
#if !GARDEN_PACK_RESOURCES
static bool tryGetShaderPath(const fs::path& appResourcesPath, const fs::path& resourcePath, fs::path& filePath)
{
	GARDEN_ASSERT(!resourcePath.empty());
	auto enginePath = GARDEN_SOURCE_PATH / resourcePath;
	auto appPath = appResourcesPath / resourcePath;
	auto hasEngineFile = fs::exists(enginePath);
	auto hasAppFile = fs::exists(appPath);

	if ((hasEngineFile && hasAppFile) || (!hasEngineFile && !hasAppFile))
		return false;

	filePath = hasEngineFile ? enginePath : appPath;
	return true;
}
#endif

static bool loadOrCompileGraphics(GslCompiler::GraphicsData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto shadersPath = "shaders" / data.shaderPath;
	auto headerPath = shadersPath; headerPath += ".gslh";
	auto vertexPath = shadersPath; vertexPath += ".vert";
	auto fragmentPath = shadersPath; fragmentPath += ".frag";

	fs::path vertexInputPath, fragmentInputPath;
	auto hasVertexShader = tryGetShaderPath(data.resourcesPath, vertexPath, vertexInputPath);
	auto hasFragmentShader = tryGetShaderPath(data.resourcesPath, fragmentPath, fragmentInputPath);

	if (!hasVertexShader && !hasFragmentShader)
	{
		throw GardenError("Graphics shader file does not exist or it is ambiguous. ("
			"path: " + data.shaderPath.generic_string() + ")");
	}

	vertexPath += ".spv"; fragmentPath += ".spv";
	auto headerFilePath = data.cachePath / headerPath;
	auto vertexOutputPath = data.cachePath / vertexPath;
	auto fragmentOutputPath = data.cachePath / fragmentPath;
	// TODO: check for .gsl, .h header changes and recompile shaders!
	
	if (!fs::exists(headerFilePath) || (hasVertexShader && (!fs::exists(vertexOutputPath) ||
		fs::last_write_time(vertexInputPath) > fs::last_write_time(vertexOutputPath))) ||
		(hasFragmentShader && (!fs::exists(fragmentOutputPath) ||
		fs::last_write_time(fragmentInputPath) > fs::last_write_time(fragmentOutputPath))))
	{
		const vector<fs::path> includePaths =
		{ GARDEN_SOURCE_PATH / "shaders", data.resourcesPath / "shaders" };
		auto dataPath = data.shaderPath;

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
			data.shaderPath = dataPath.filename();
			compileResult = GslCompiler::compileGraphicsShaders(inputPath, outputPath, includePaths, data);
		}
		catch (const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << vertexInputPath.generic_string() + "(.frag): " + e.what() + "\n"; // TODO: get info which stage throw.
			GARDEN_LOG_ERROR("Failed to compile graphics shaders. (name: " + dataPath.generic_string() + ")");
			fs::remove(headerFilePath);
			return false;
		}
		
		if (!compileResult)
			throw GardenError("Shader files does not exist. (path: " + dataPath.generic_string() + ")");
		GARDEN_LOG_DEBUG("Compiled graphics shaders. (path: " + dataPath.generic_string() + ")");
		return true;
	}
	#endif

	try
	{
		GslCompiler::loadGraphicsShaders(data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load graphics shaders. (name: " + 
			data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> ResourceSystem::loadGraphicsPipeline(const fs::path& path,
	ID<Framebuffer> framebuffer, const GraphicsLoadOptions* options)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(framebuffer, "Assert " + path.generic_string());
	// TODO: validate specConstValues and stateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineVersion = graphicsAPI->graphicsPipelineVersion++;
	auto loadOptions = options ? *options : GraphicsLoadOptions();

	auto pipeline = graphicsAPI->graphicsPipelinePool.create(path, 
		loadOptions.maxBindlessCount, pipelineVersion, framebuffer);

	auto framebufferView = graphicsAPI->framebufferPool.get(framebuffer);
	const auto& colorAttachments = framebufferView->getColorAttachments();
	auto colorAttachmentCount = (uint32)colorAttachments.size();
	auto colorAttachmentData = colorAttachments.data();

	vector<Image::Format> colorFormats(colorAttachmentCount);
	auto colorFormatData = colorFormats.data();
	for (uint32 i = 0; i < colorAttachmentCount; i++)
	{
		auto imageView = colorAttachmentData[i].imageView;
		if (!imageView)
		{
			colorFormatData[i] = Image::Format::Undefined;
			continue;
		}
		auto attachment = graphicsAPI->imageViewPool.get(imageView);
		colorFormatData[i] = attachment->getFormat();
	}

	auto depthStencilFormat = Image::Format::Undefined;
	if (framebufferView->getDepthStencilAttachment().imageView)
	{
		auto attachment = graphicsAPI->imageViewPool.get(
			framebufferView->getDepthStencilAttachment().imageView);
		depthStencilFormat = attachment->getFormat();
	}

	auto threadSystem = ThreadSystem::tryGetInstance();
	if (loadOptions.loadAsync && threadSystem && !loadOptions.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!loadOptions.shaderOverrides, "Nothing to load asynchronously");
	
		auto data = new GraphicsPipelineLoadData();
		data->shaderPath = path;
		data->pipelineVersion = pipelineVersion;
		data->colorFormats = std::move(colorFormats);
		if (loadOptions.specConstValues)
			data->specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		if (loadOptions.pipelineStateOverrides)
			data->pipelineStateOverrides = std::move(*loadOptions.pipelineStateOverrides);
		if (loadOptions.blendStateOverrides)
			data->blendStateOverrides = std::move(*loadOptions.blendStateOverrides);
		data->instance = pipeline;
		data->maxBindlessCount = loadOptions.maxBindlessCount;
		data->depthStencilFormat = depthStencilFormat;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachePath = appCachePath;
		#endif
		
		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Graphics Pipeline Load");

			GslCompiler::GraphicsData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->pipelineVersion;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			pipelineData.colorFormats = std::move(data->colorFormats);
			pipelineData.pipelineStateOverrides = std::move(data->pipelineStateOverrides);
			pipelineData.blendStateOverrides = std::move(data->blendStateOverrides);
			pipelineData.depthStencilFormat = data->depthStencilFormat;
			#if GARDEN_PACK_RESOURCES
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#else
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachePath = std::move(data->cachePath);
			#endif

			if (!loadOrCompileGraphics(pipelineData))
			{
				delete data;
				return;
			}

			GraphicsQueueItem item =
			{
				GraphicsPipelineExt::create(pipelineData),
				data->instance
			};

			queueLocker.lock();
			loadedGraphicsQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		loadOptions.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Graphics Pipeline Load");

		GslCompiler::GraphicsData pipelineData;
		if (loadOptions.specConstValues)
			pipelineData.specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		if (loadOptions.pipelineStateOverrides)
			pipelineData.pipelineStateOverrides = std::move(*loadOptions.pipelineStateOverrides);
		if (loadOptions.blendStateOverrides)
			pipelineData.blendStateOverrides = std::move(*loadOptions.blendStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = loadOptions.maxBindlessCount;
		pipelineData.colorFormats = std::move(colorFormats);
		pipelineData.depthStencilFormat = depthStencilFormat;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif

		if (loadOptions.shaderOverrides)
		{
			pipelineData.headerData = std::move(loadOptions.shaderOverrides->headerData);
			pipelineData.vertexCode = std::move(loadOptions.shaderOverrides->vertexCode);
			pipelineData.fragmentCode = std::move(loadOptions.shaderOverrides->fragmentCode);
			GslCompiler::loadGraphicsShaders(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileGraphics(pipelineData))
			{
				graphicsAPI->graphicsPipelinePool.destroy(pipeline);
				throw GardenError("Failed to load graphics pipeline. (path: " + path.generic_string() + ")");
			}
		}
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData);
		auto pipelineView = graphicsAPI->graphicsPipelinePool.get(pipeline);
		GraphicsPipelineExt::moveInternalObjects(graphicsPipeline, **pipelineView);
		GARDEN_LOG_TRACE("Loaded graphics pipeline. (path: " +  path.generic_string() + ")");
	}

	return pipeline;
}

//**********************************************************************************************************************
static bool loadOrCompileCompute(GslCompiler::ComputeData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto shadersPath = "shaders" / data.shaderPath;
	auto headerPath = shadersPath; headerPath += ".gslh";
	auto computePath = shadersPath; computePath += ".comp";

	fs::path computeInputPath;
	if (!tryGetShaderPath(data.resourcesPath, computePath, computeInputPath))
	{
		throw GardenError("Compute shader file does not exist, or it is ambiguous. ("
			"path: " + data.shaderPath.generic_string() + ")");
	}

	computePath += ".spv";
	auto headerFilePath = data.cachePath / headerPath;
	auto computeOutputPath = data.cachePath / computePath;
	
	if (!fs::exists(headerFilePath) || !fs::exists(computeOutputPath) ||
		fs::last_write_time(computeInputPath) > fs::last_write_time(computeOutputPath))
	{
		const vector<fs::path> includePaths =
		{ GARDEN_SOURCE_PATH / "shaders", data.resourcesPath / "shaders" };
		auto dataPath = data.shaderPath;

		auto compileResult = false;
		try
		{
			data.shaderPath = dataPath.filename();
			compileResult = GslCompiler::compileComputeShader(computeInputPath.parent_path(),
				computeOutputPath.parent_path(), includePaths, data);
		}
		catch (const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << computeInputPath.generic_string() + ": " + e.what() + "\n";
			GARDEN_LOG_ERROR("Failed to compile compute shader. (name: " + dataPath.generic_string() + ")");
			fs::remove(headerFilePath);
			return false;
		}
		
		if (!compileResult)
			throw GardenError("Shader file does not exist. (path: " + dataPath.generic_string() + ")");
		GARDEN_LOG_DEBUG("Compiled compute shader. (path: " + dataPath.generic_string() + ")");
		return true;
	}
	#endif

	try
	{
		GslCompiler::loadComputeShader(data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load compute shader. (name: " + 
			data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<ComputePipeline> ResourceSystem::loadComputePipeline(const fs::path& path, const ComputeLoadOptions* options)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineVersion = graphicsAPI->computePipelineVersion++;
	auto loadOptions = options ? *options : ComputeLoadOptions();

	auto pipeline = graphicsAPI->computePipelinePool.create(
		path, loadOptions.maxBindlessCount, pipelineVersion);

	auto threadSystem = ThreadSystem::tryGetInstance();
	if (loadOptions.loadAsync && threadSystem && !loadOptions.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!loadOptions.shaderOverrides, "Nothing to load asynchronously");

		auto data = new ComputePipelineLoadData();
		data->pipelineVersion = pipelineVersion;
		data->shaderPath = path;
		if (loadOptions.specConstValues)
			data->specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		data->maxBindlessCount = loadOptions.maxBindlessCount;
		data->instance = pipeline;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachePath = appCachePath;
		#endif

		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Compute Pipeline Load");

			GslCompiler::ComputeData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->pipelineVersion;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			#if GARDEN_PACK_RESOURCES
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#else
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachePath = std::move(data->cachePath);
			#endif
			
			if (!loadOrCompileCompute(pipelineData))
			{
				delete data;
				return;
			}

			ComputeQueueItem item = 
			{
				ComputePipelineExt::create(pipelineData),
				data->instance
			};

			queueLocker.lock();
			loadedComputeQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		loadOptions.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Compute Pipeline Load");

		GslCompiler::ComputeData pipelineData;
		if (loadOptions.specConstValues)
			pipelineData.specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = loadOptions.maxBindlessCount;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif
		
		if (loadOptions.shaderOverrides)
		{
			pipelineData.headerData = std::move(loadOptions.shaderOverrides->headerData);
			pipelineData.code = std::move(loadOptions.shaderOverrides->code);
			GslCompiler::loadComputeShader(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileCompute(pipelineData))
			{
				graphicsAPI->computePipelinePool.destroy(pipeline);
				throw GardenError("Failed to load compute pipeline. (path: " + path.generic_string() + ")");
			}
		}

		auto computePipeline = ComputePipelineExt::create(pipelineData);
		auto pipelineView = graphicsAPI->computePipelinePool.get(pipeline);
		ComputePipelineExt::moveInternalObjects(computePipeline, **pipelineView);
		GARDEN_LOG_TRACE("Loaded compute pipeline. (path: " + path.generic_string() + ")");
	}

	return pipeline;
}

//**********************************************************************************************************************
static bool loadOrCompileRayTracing(GslCompiler::RayTracingData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto shadersPath = "shaders" / data.shaderPath;
	auto headerPath = shadersPath; headerPath += ".gslh";
	auto rayGenerationPath = shadersPath; rayGenerationPath += ".rgen";
	auto missPath = shadersPath; missPath += ".rmiss";
	auto callablePath = shadersPath; callablePath += ".rcall";
	auto intersectionPath = shadersPath; intersectionPath += ".rint";
	auto anyHitPath = shadersPath; anyHitPath += ".rahit";
	auto closestHitPath = shadersPath; closestHitPath += ".rchit";

	fs::path rayGenInputPath, missInputPath, intersectInputPath, closHitInputPath, anyHitInputPath, callInputPath;
	auto hasRayGenShader = tryGetShaderPath(data.resourcesPath, rayGenerationPath, rayGenInputPath);
	auto hasMissShader = tryGetShaderPath(data.resourcesPath, missPath, missInputPath);
	auto hasCallableShader = tryGetShaderPath(data.resourcesPath, callablePath, callInputPath);
	auto hasIntersectShader = tryGetShaderPath(data.resourcesPath, intersectionPath, intersectInputPath);
	auto hasAnyHitShader = tryGetShaderPath(data.resourcesPath, anyHitPath, anyHitInputPath);
	auto hasClosHitShader = tryGetShaderPath(data.resourcesPath, closestHitPath, closHitInputPath);

	if (!hasRayGenShader || !hasMissShader || (!hasIntersectShader && !hasClosHitShader && !hasAnyHitShader))
	{
		throw GardenError("Ray tracing shader file does not exist or it is ambiguous. ("
			"path: " + data.shaderPath.generic_string() + ")");
	}

	rayGenerationPath += ".spv"; missPath += ".spv"; callablePath += ".spv";
	intersectionPath += ".spv"; anyHitPath += ".spv"; closestHitPath += ".spv"; 

	auto headerFilePath = data.cachePath / headerPath;
	auto rayGenOutputPath = data.cachePath / rayGenerationPath;
	auto missOutputPath = data.cachePath / missPath;
	auto callOutputPath = data.cachePath / callablePath;
	auto intersectOutputPath = data.cachePath / intersectionPath;
	auto anyHitOutputPath = data.cachePath / anyHitPath;
	auto closHitOutputPath = data.cachePath / closestHitPath;

	// !!! TODO: check for changes in additional ray tracing shader hit groups shaders. rt-shader.1.rchit.spv

	if (!fs::exists(headerFilePath) ||
		(!fs::exists(rayGenOutputPath) || fs::last_write_time(rayGenInputPath) > fs::last_write_time(rayGenOutputPath)) ||
		(!fs::exists(missOutputPath) || fs::last_write_time(missInputPath) > fs::last_write_time(missOutputPath)) ||
		(hasCallableShader && (!fs::exists(callOutputPath) ||
		fs::last_write_time(callInputPath) > fs::last_write_time(callOutputPath))) ||
		(hasIntersectShader && (!fs::exists(intersectOutputPath) ||
		fs::last_write_time(intersectInputPath) > fs::last_write_time(intersectOutputPath))) ||
		(hasAnyHitShader && (!fs::exists(anyHitOutputPath) ||
		fs::last_write_time(anyHitInputPath) > fs::last_write_time(anyHitOutputPath))) ||
		(hasClosHitShader && (!fs::exists(closHitOutputPath) ||
		fs::last_write_time(closHitInputPath) > fs::last_write_time(closHitOutputPath))))
	{
		const vector<fs::path> includePaths =
		{ GARDEN_SOURCE_PATH / "shaders", data.resourcesPath / "shaders" };
		auto dataPath = data.shaderPath;

		auto compileResult = false;
		try
		{
			data.shaderPath = dataPath.filename();
			compileResult = GslCompiler::compileRayTracingShaders(rayGenInputPath.parent_path(), 
				rayGenOutputPath.parent_path(), includePaths, data);
		}
		catch (const exception& e)
		{
			if (strcmp(e.what(), "_GLSLC") != 0)
				cout << rayGenInputPath.generic_string() + "(.rXXX): " + e.what() + "\n"; // TODO: get info which stage throw.
			GARDEN_LOG_ERROR("Failed to compile ray tracing shaders. (name: " + dataPath.generic_string() + ")");
			fs::remove(headerFilePath);
			return false;
		}
		
		if (!compileResult)
			throw GardenError("Shader files does not exist. (path: " + dataPath.generic_string() + ")");
		GARDEN_LOG_DEBUG("Compiled ray tracing shaders. (path: " + dataPath.generic_string() + ")");
		return true;
	}
	#endif

	try
	{
		GslCompiler::loadRayTracingShaders(data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to load ray tracing shaders. (name: " + 
			data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<RayTracingPipeline> ResourceSystem::loadRayTracingPipeline(
	const fs::path& path, const RayTracingLoadOptions* options)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineVersion = graphicsAPI->rayTracingPipelineVersion++;
	auto loadOptions = options ? *options : RayTracingLoadOptions();

	auto pipeline = graphicsAPI->rayTracingPipelinePool.create(
		path, loadOptions.maxBindlessCount, pipelineVersion);

	auto threadSystem = ThreadSystem::tryGetInstance();
	if (loadOptions.loadAsync && threadSystem && !loadOptions.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!loadOptions.shaderOverrides, "Nothing to load asynchronously");
	
		auto data = new RayTracingPipelineLoadData();
		data->pipelineVersion = pipelineVersion;
		data->shaderPath = path;
		if (loadOptions.specConstValues)
			data->specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		data->maxBindlessCount = loadOptions.maxBindlessCount;
		data->instance = pipeline;
		#if !GARDEN_PACK_RESOURCES
		data->resourcesPath = appResourcesPath;
		data->cachePath = appCachePath;
		#endif

		threadSystem->getBackgroundPool().addTask([this, data](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("Ray Tracing Pipeline Load");

			GslCompiler::RayTracingData pipelineData;
			pipelineData.shaderPath = std::move(data->shaderPath);
			pipelineData.specConstValues = std::move(data->specConstValues);
			pipelineData.samplerStateOverrides = std::move(data->samplerStateOverrides);
			pipelineData.pipelineVersion = data->pipelineVersion;
			pipelineData.maxBindlessCount = data->maxBindlessCount;
			#if GARDEN_PACK_RESOURCES
			pipelineData.packReader = &packReader;
			pipelineData.threadIndex = task.getThreadIndex();
			#else
			pipelineData.resourcesPath = std::move(data->resourcesPath);
			pipelineData.cachePath = std::move(data->cachePath);
			#endif
			
			if (!loadOrCompileRayTracing(pipelineData))
			{
				delete data;
				return;
			}

			RayTracingQueueItem item = 
			{
				RayTracingPipelineExt::create(pipelineData),
				data->instance
			};

			queueLocker.lock();
			loadedRayTracingQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		loadOptions.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Ray Tracing Pipeline Load");

		GslCompiler::RayTracingData pipelineData;
		if (loadOptions.specConstValues)
			pipelineData.specConstValues = std::move(*loadOptions.specConstValues);
		if (loadOptions.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*loadOptions.samplerStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = loadOptions.maxBindlessCount;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif
		
		if (loadOptions.shaderOverrides)
		{
			pipelineData.headerData = std::move(loadOptions.shaderOverrides->headerData);
			pipelineData.rayGenGroups = std::move(loadOptions.shaderOverrides->rayGenGroups);
			pipelineData.missGroups = std::move(loadOptions.shaderOverrides->missGroups);
			pipelineData.callGroups = std::move(loadOptions.shaderOverrides->callGroups);
			pipelineData.hitGroups = std::move(loadOptions.shaderOverrides->hitGroups);
			GslCompiler::loadRayTracingShaders(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileRayTracing(pipelineData))
			{
				graphicsAPI->rayTracingPipelinePool.destroy(pipeline);
				throw GardenError("Failed to load ray tracing pipeline. (path: " + path.generic_string() + ")");
			}
		}

		auto rayTracingPipeline = RayTracingPipelineExt::create(pipelineData);
		auto pipelineView = graphicsAPI->rayTracingPipelinePool.get(pipeline);
		RayTracingPipelineExt::moveInternalObjects(rayTracingPipeline, **pipelineView);
		GARDEN_LOG_TRACE("Loaded ray tracing pipeline. (path: " + path.generic_string() + ")");
	}

	return pipeline;
}

//**********************************************************************************************************************
ID<Entity> ResourceSystem::loadScene(const fs::path& path, bool addRootEntity)
{
	GARDEN_ASSERT(!path.empty());

	auto manager = Manager::getInstance();
	auto systemGroup = manager->tryGetSystemGroup<ISerializable>();
	if (!systemGroup)
	{
		GARDEN_LOG_ERROR("No ISerializable system found.");
		return {};
	}

	JsonDeserializer deserializer;
	fs::path filePath = "scenes" / path; filePath += ".scene";

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0; vector<uint8> dataBuffer;
	if (!packReader.getItemIndex(filePath, itemIndex))
	{
		GARDEN_LOG_ERROR("Scene file does not exist. (path: " + path.generic_string() + ")");
		return {};
	}
	packReader.readItemData(itemIndex, dataBuffer);

	try
	{
		deserializer.load(dataBuffer);
		dataBuffer = {};
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize scene. (path: " + path.generic_string() + ")");
		return {};
	}
	#else
	fs::path scenePath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, scenePath))
	{
		GARDEN_LOG_ERROR("Scene file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(scenePath);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize scene. (path: " + 
			path.generic_string() + ", error: " + string(e.what()) + ")");
		return {};
	}
	#endif

	ID<Entity> rootEntity = {};
	if (addRootEntity)
	{
		if (TransformSystem::hasInstance())
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

	for (auto system : *systemGroup)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(system);
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
					GARDEN_LOG_ERROR("Missing scene entity components. (path: " + 
						path.generic_string() + ", entity: " + to_string(i) + ")");
					continue;
				}

				auto entity = manager->createEntity();
				manager->reserveComponents(entity, componentCount);

				for (uint32 j = 0; j < componentCount; j++)
				{
					if (!deserializer.beginArrayElement(j))
						break;

					if (!deserializer.read(".type", type))
					{
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Missing scene component type. (path: " + path.generic_string() + 
							", entity: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto result = componentNames.find(type);
					if (result == componentNames.end())
					{
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Unknown scene component type. (path: " + path.generic_string() + 
							", entity: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto system = result->second;
					auto serializableSystem = dynamic_cast<ISerializable*>(system);
					if (!serializableSystem)
					{
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Not serializable scene system. (path: " + path.generic_string() + 
							", entity: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto componentView = manager->add(entity, system->getComponentType());
					serializableSystem->deserialize(deserializer, componentView);
					deserializer.endArrayElement();
				}

				if (!manager->hasComponents(entity))
				{
					GARDEN_LOG_ERROR("Missing scene entity components. (path: " + 
						path.generic_string() + ", entity: " + to_string(i) + ")");
					manager->destroy(entity);
				}
				else
				{
					if (addRootEntity)
					{
						auto transformView = manager->tryGet<TransformComponent>(entity);
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

	for (auto system : *systemGroup)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(system);
		serializableSystem->postDeserialize(deserializer);
	}

	if (addRootEntity)
	{
		// Note: Reducing root component memory consumption after serialization completion.
		auto transformView = manager->tryGet<TransformComponent>(rootEntity);
		if (transformView)
			transformView->shrinkChilds();
	}

	GARDEN_LOG_TRACE("Loaded scene. (path: " + path.generic_string() + ")");
	return rootEntity;
}

//**********************************************************************************************************************
void ResourceSystem::storeScene(const fs::path& path, ID<Entity> rootEntity, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());

	auto manager = Manager::getInstance();
	auto systemGroup = manager->tryGetSystemGroup<ISerializable>();
	if (!systemGroup)
	{
		GARDEN_LOG_ERROR("No ISerializable system found.");
		return;
	}

	#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
	auto scenesPath = directory.empty() ? appResourcesPath / "scenes" : directory;
	#else
	const auto& scenesPath = directory;
	#endif

	auto directoryPath = scenesPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = scenesPath / path; filePath += ".scene";
	JsonSerializer serializer(filePath);

	for (auto system : *systemGroup)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(system);
		serializableSystem->preSerialize(serializer);
	}

	serializer.write("version", appVersion.toString3());
	serializer.beginChild("entities");
	
	const auto& entities = manager->getEntities();
	auto dnsSystem = DoNotSerializeSystem::tryGetInstance();
	stack<ID<Entity>, vector<ID<Entity>>> childEntities;

	ID<Entity> rootParent = {};
	if (rootEntity)
	{
		auto transformView = manager->tryGet<TransformComponent>(rootEntity);
		if (transformView)
		{
			rootParent = transformView->getParent();
			transformView->setParent({});
		}
		else rootEntity = {};

		childEntities.push(rootEntity);
	}
	else
	{
		for (const auto& entityView : entities)
		{
			if (!entityView.hasComponents())
				continue;

			auto entity = entities.getID(&entityView);
			auto transformView = manager->tryGet<TransformComponent>(entity);
			if (transformView && transformView->getParent())
				continue;

			childEntities.push(entity);
		}
	}
	
	while (!childEntities.empty())
	{
		auto entity = childEntities.top(); childEntities.pop();
		auto transformView = manager->tryGet<TransformComponent>(entity);

		if (rootEntity)
		{
			if (!transformView || (entity != rootEntity && !transformView->hasAncestor(rootEntity)))
				continue;
		}
		else
		{
			if (dnsSystem && dnsSystem->hasOrAncestors(entity))
				continue;
		}

		if (transformView)
		{
			auto childs = transformView->getChilds();
			for (int64 i = (int64)transformView->getChildCount() - 1; i >= 0; i--)
				childEntities.push(childs[i]); // Note: reversed.
		}

		serializer.beginArrayElement();
		serializer.beginChild("components");

		auto entityView = entities.get(entity);
		auto components = entityView->getComponents();
		auto componentCount = entityView->getComponentCount();

		for (uint32 i = 0; i < componentCount; i++)
		{
			auto& component = components[i];
			auto system = component.system;

			if (system->getComponentType() == typeid(DoNotDestroyComponent) || 
				system->getComponentType() == typeid(DoNotDuplicateComponent))
			{
				serializer.beginArrayElement();
				serializer.write(".type", system->getComponentName());
				serializer.endArrayElement();
				continue;
			}

			auto serializableSystem = dynamic_cast<ISerializable*>(system);
			auto componentName = system->getComponentName();
			if (!serializableSystem || componentName.empty())
				continue;
			
			serializer.beginArrayElement();
			serializer.write(".type", componentName);
			auto componentView = system->getComponent(component.instance);
			serializableSystem->serialize(serializer, componentView);
			serializer.endArrayElement();
		}

		serializer.endChild();
		serializer.endArrayElement();
	}

	serializer.endChild();

	for (auto system : *systemGroup)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(system);
		serializableSystem->postSerialize(serializer);
	}

	if (rootEntity)
	{
		auto transformView = manager->get<TransformComponent>(rootEntity);
		transformView->setParent(rootParent);
	}

	GARDEN_LOG_TRACE("Stored scene. (path: " + path.generic_string() + ")");
}

//**********************************************************************************************************************
void ResourceSystem::clearScene()
{
	auto manager = Manager::getInstance();
	const auto& entities = manager->getEntities();

	for (const auto& entity : entities)
	{
		auto entityID = entities.getID(&entity);
		if (!entity.hasComponents() || manager->has<DoNotDestroyComponent>(entityID))
			continue;

		auto transformView = manager->tryGet<TransformComponent>(entityID);
		if (transformView)
			transformView->setParent({});
		manager->destroy(entityID);
	}

	GARDEN_LOG_TRACE("Cleaned scene.");
}

//**********************************************************************************************************************
ID<Animation> ResourceSystem::loadAnimation(const fs::path& path)
{
	GARDEN_ASSERT(!path.empty());
	JsonDeserializer deserializer;
	fs::path filePath = "animations" / path; filePath += ".anim";

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0; vector<uint8> dataBuffer;
	if (!packReader.getItemIndex(filePath, itemIndex))
	{
		GARDEN_LOG_ERROR("Animation file does not exist. (path: " + path.generic_string() + ")");
		return {};
	}
	packReader.readItemData(itemIndex, dataBuffer);

	try
	{
		deserializer.load(dataBuffer);
		dataBuffer = {};
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize animation. (path: " + path.generic_string() + ")");
		return {};
	}
	#else
	fs::path animationPath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, animationPath))
	{
		GARDEN_LOG_ERROR("Animation file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		deserializer.load(animationPath);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize animation. (path: " + 
			path.generic_string() + ", error: " + string(e.what()) + ")");
		return {};
	}
	#endif

	auto animationSystem = AnimationSystem::getInstance();
	auto animation = animationSystem->createAnimation();
	auto animationView = animationSystem->get(animation);

	deserializer.read("frameRate", animationView->frameRate);
	deserializer.read("isLooped", animationView->isLooped);

	if (deserializer.beginChild("keyframes"))
	{
		const auto& componentNames = Manager::getInstance()->getComponentNames();
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
						GARDEN_LOG_ERROR("Missing animation component type. (path: " + path.generic_string() + 
							", keyframe: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto result = componentNames.find(type);
					if (result == componentNames.end())
					{
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Unknown animation component type. (path: " + path.generic_string() + 
							", keyframe: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto system = result->second;
					auto animatableSystem = dynamic_cast<IAnimatable*>(system);
					if (!animatableSystem)
					{
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Not animatable system. (path: " + path.generic_string() + 
							", keyframe: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					auto animationFrame = animatableSystem->createAnimation();
					auto animationFrameView = animatableSystem->getAnimation(animationFrame);
					animatableSystem->deserializeAnimation(deserializer, animationFrameView);

					if (!animationFrameView->hasAnimation())
					{
						animatableSystem->destroyAnimation(animationFrame);
						deserializer.endArrayElement();
						GARDEN_LOG_ERROR("Missing keyframe animation. (path: " + path.generic_string() + 
							", keyframe: " + to_string(i) + ", component: " + to_string(j) + ")");
						continue;
					}

					deserializer.read(".funcCoeff", animationFrameView->funcCoeff);

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

				if (animatables.empty())
				{
					GARDEN_LOG_ERROR("Missing keyframe animatables. (path: " + 
						path.generic_string() + ", keyframe: " + to_string(i) + ")");
				}
				else
				{
					animationView->emplaceKeyframe(frame, std::move(animatables));
				}
				deserializer.endChild();
			}
			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	if (animationView->getKeyframes().empty())
	{
		animationSystem->destroy(animation);
		GARDEN_LOG_ERROR("Missing animation keyframes. (path: " + path.generic_string() + ")");
		return {};
	}

	GARDEN_LOG_TRACE("Loaded animation. (path: " + path.generic_string() + ")");
	return animation;
}
Ref<Animation> ResourceSystem::loadSharedAnimation(const fs::path& path)
{
	GARDEN_ASSERT(!path.empty());
	auto pathString = path.generic_string();
	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, pathString.c_str(), pathString.length());
	auto hash = Hash128::digestState(hashState);

	auto searchResult = sharedAnimations.find(hash);
	if (searchResult != sharedAnimations.end())
		return searchResult->second;

	auto animation = Ref<Animation>(loadAnimation(path)); 
	auto result = sharedAnimations.emplace(hash, animation);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	return animation;
}
void ResourceSystem::destroyShared(Ref<Animation>& animation)
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

	AnimationSystem::getInstance()->destroy(animation);
}

//**********************************************************************************************************************
void ResourceSystem::storeAnimation(const fs::path& path, ID<Animation> animation, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(animation, "Assert " + path.generic_string());

	#if !GARDEN_PACK_RESOURCES || GARDEN_EDITOR
	auto animationsPath = directory.empty() ? appResourcesPath / "animations" : directory;
	#else
	const auto& animationsPath = directory;
	#endif

	auto directoryPath = animationsPath / path.parent_path();
	if (!fs::exists(directoryPath))
		fs::create_directories(directoryPath);

	auto filePath = animationsPath / path; filePath += ".anim";
	JsonSerializer serializer(filePath);

	const auto animationView = AnimationSystem::getInstance()->get(animation);
	if (animationView->frameRate != 30.0f)
		serializer.write("frameRate", animationView->frameRate);
	if (!animationView->isLooped)
		serializer.write("isLooped", false);

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
			auto componentName = system->getComponentName();
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

			if (frameView->funcCoeff != 1.0f)
				serializer.write(".funcCoeff", frameView->funcCoeff);
			
			animatableSystem->serializeAnimation(serializer, frameView);
			serializer.endArrayElement();
		}
		serializer.endChild();

		serializer.endArrayElement();
	}
	serializer.endChild();

	GARDEN_LOG_TRACE("Stored animation. (path: " + path.generic_string() + ")");
}

//**********************************************************************************************************************
Ref<Font> ResourceSystem::loadFont(const fs::path& path, int32 faceIndex, bool logMissing)
{
	GARDEN_ASSERT(!path.empty());
	
	auto pathString = path.generic_string();
	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, pathString.c_str(), pathString.length());
	auto hash = Hash128::digestState(hashState);

	auto result = sharedFonts.find(hash);
	if (result != sharedFonts.end())
		return result->second;

	auto textSystem = TextSystem::getInstance();
	fs::path filePath = "fonts" / path; filePath += ".ttf";
	vector<uint8> fontData;

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0;
	if (!packReader.getItemIndex(filePath, itemIndex))
	{
		if (logMissing)
			GARDEN_LOG_ERROR("Font does not exist. (path: " + path.generic_string() + ")");
		return {};
	}
	packReader.readItemData(itemIndex, fontData);
	#else
	fs::path fontPath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, fontPath))
	{
		if (logMissing)
			GARDEN_LOG_ERROR("Font file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	try
	{
		File::loadBinary(filePath, fontData);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR(string(e.what()));
		return {};
	}
	#endif

	auto ftLibrary = (FT_Library)textSystem->ftLibrary;
	auto threadSystem = ThreadSystem::tryGetInstance();
	vector<void*> faces(threadSystem ? threadSystem->getForegroundPool().getThreadCount() : 1);

	for (auto& face : faces)
	{
		FT_Face ftFace = nullptr;
		auto result = FT_New_Memory_Face(ftLibrary, fontData.data(), fontData.size(), faceIndex, &ftFace);
		if (result != 0)
		{
			GARDEN_LOG_ERROR("Failed to load font. (path: " + path.generic_string() + 
				", error: " + string(FT_Error_String(result)) + ")");
			for (auto _face : faces)
			{
				if (!_face) continue;
				result = FT_Done_Face((FT_Face)_face);
				GARDEN_ASSERT_MSG(!result, "Failed to destroy FreeType font");
			}
			return {};
		}
		face = ftFace;
	}

	auto font = Ref<Font>(textSystem->fonts.create());
	auto emplaceResult = sharedFonts.emplace(hash, font);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");

	auto fontView = textSystem->fonts.get(font);
	fontView->faces = std::move(faces);
	fontView->data = std::move(fontData);

	GARDEN_LOG_TRACE("Loaded font. (path: " + path.generic_string() + ")");
	return font;
}

//**********************************************************************************************************************
FontArray ResourceSystem::loadFonts(const vector<fs::path>& paths, int32 faceIndex, bool loadNoto)
{
	auto fontPaths = paths.empty() ? defaultFontPaths : paths;
	if (loadNoto)
		fontPaths.insert(fontPaths.end(), notoFontPaths.begin(), notoFontPaths.end());
	GARDEN_ASSERT(!fontPaths.empty());

	FontArray fonts(4);
	for (const auto& path : fontPaths)
	{
		auto font = loadFont(path, faceIndex, false);
		if (font)
		{
			fonts[0].push_back(font); fonts[1].push_back(font);
			fonts[2].push_back(font); fonts[3].push_back(font);
			continue;
		}

		auto fontPath = path; fontPath /= "regular";
		font = loadFont(fontPath, faceIndex);
		if (font) fonts[0].push_back(font);

		fontPath = path; fontPath /= "bold";
		font = loadFont(fontPath, faceIndex);
		if (font) fonts[1].push_back(font);

		fontPath = path; fontPath /= "italic";
		font = loadFont(fontPath, faceIndex);
		if (font) fonts[2].push_back(font);

		fontPath = path; fontPath /= "bold-italic";
		font = loadFont(fontPath, faceIndex);
		if (font) fonts[3].push_back(font);
	}

	if (fonts[0].empty() || fonts[1].empty() || fonts[2].empty() || fonts[3].empty())
	{
		destroyShared(fonts);
		return {};
	}
	return fonts;
}

void ResourceSystem::destroyShared(Ref<Font>& font)
{
	if (!font || font.getRefCount() > 2)
		return;

	for (auto i = sharedFonts.begin(); i != sharedFonts.end(); i++)
	{
		if (i->second != font)
			continue;
		sharedFonts.erase(i);
		break;
	}

	auto item = ID<Font>(font); font = {};
	TextSystem::getInstance()->fonts.destroy(item);
}
void ResourceSystem::destroyShared(FontArray& fonts)
{
	for (auto& variants : fonts)
	{
		for (auto& font : variants)
			destroyShared(font);
	}
}

//**********************************************************************************************************************
bool ResourceSystem::loadData(const fs::path& path, vector<uint8>& data)
{
	GARDEN_ASSERT(!path.empty());

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0;
	if (!packReader.getItemIndex(path, itemIndex))
	{
		GARDEN_LOG_ERROR("Resource file does not exist. (path: " + path.generic_string() + ")");
		return false;
	}
	packReader.readItemData(itemIndex, data);
	#else
	fs::path resourcePath;
	if (!File::tryGetResourcePath(appResourcesPath, path, resourcePath))
	{
		GARDEN_LOG_ERROR("Resource file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return false;
	}

	try
	{
		File::loadBinary(resourcePath, data);
	}
	catch (const exception& e)
	{
		GARDEN_LOG_ERROR(string(e.what()));
		return false;
	}
	#endif
	return true;
}
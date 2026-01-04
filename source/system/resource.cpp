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
#include "garden/system/thread.hpp"
#include "garden/system/text.hpp"
#include "garden/system/log.hpp"
#include "garden/graphics/equi2cube.hpp"
#include "garden/graphics/gslc.hpp"
#include "garden/graphics/api.hpp"
#include "garden/graphics/exr.hpp"
#include "garden/json-serialize.hpp"
#include "garden/json2bson.hpp"
#include "garden/profiler.hpp"
#include "garden/file.hpp"
#include "math/tone-mapping.hpp"
#include "math/types.hpp"

#include "webp/decode.h"
#include "webp/encode.h"
#include "png.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include "ft2build.h"
#include FT_FREETYPE_H

#include <fstream>
#include <cstdint>
#include <cstdlib>
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
		Image::Usage usage = {};
		Image::Strategy strategy = {};
		uint8 maxMipCount = 0;
		ImageLoadFlags flags = {};
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
		BufferLoadFlags flags = {};
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
		void* renderPass = nullptr;
		vector<Image::Format> colorFormats;
		GraphicsPipeline::PipelineStates pipelineStateOverrides;
		GraphicsPipeline::BlendStates blendStateOverrides;
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
	struct RayTracingPipelineLoadData final : public PipelineLoadData
	{
		ID<RayTracingPipeline> instance = {};
		bool useAsyncRecording = false;
	};
}

const vector<string_view> ResourceSystem::imageFileExts =
{
	".webp", ".png", ".jpg", ".jpeg", ".exr", ".hdr", ".bmp", ".psd", ".tga", ".pic", ".gif"
};
const vector<ImageFileType> ResourceSystem::imageFileTypes =
{
	ImageFileType::Webp, ImageFileType::Png, ImageFileType::Jpg, ImageFileType::Jpg, 
	ImageFileType::Exr, ImageFileType::Hdr, ImageFileType::Bmp, ImageFileType::Psd, 
	ImageFileType::Tga, ImageFileType::Pic, ImageFileType::Gif
};

const vector<string_view> ResourceSystem::modelFileExts =
{
	".fbx", ".dae", ".gltf", ".glb", ".blend", ".3ds", ".ase", ".obj"
};

//**********************************************************************************************************************
ResourceSystem::ResourceSystem(bool setSingleton) : Singleton(setSingleton)
{
	static_assert(std::numeric_limits<float>::is_iec559, "Floats are not IEEE 754");

	auto manager = Manager::Instance::get();
	manager->registerEvent("ImageLoaded");
	manager->registerEvent("BufferLoaded");

	ECSM_SUBSCRIBE_TO_EVENT("Init", ResourceSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ResourceSystem::deinit);

	auto appInfoSystem = AppInfoSystem::Instance::get();
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
ResourceSystem::~ResourceSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ResourceSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ResourceSystem::deinit);

		manager->unregisterEvent("ImageLoaded");
		manager->unregisterEvent("BufferLoaded");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
void ResourceSystem::init()
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Input", ResourceSystem::input);
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	ECSM_TRY_SUBSCRIBE_TO_EVENT("FileChange", ResourceSystem::fileChange);
	#endif
}
void ResourceSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		GraphicsAPI::get()->forceResourceDestroy = true;
		while (!loadedImageQueue.empty())
		{
			auto& item = loadedImageQueue.front();
			BufferExt::destroy(item.staging);
			ImageExt::destroy(item.image);
			loadedImageQueue.pop();
		}
		while (!loadedBufferQueue.empty())
		{
			auto& item = loadedBufferQueue.front();
			BufferExt::destroy(item.staging);
			BufferExt::destroy(item.buffer);
			loadedBufferQueue.pop();
		}
		while (!loadedRayTracingQueue.empty())
		{
			PipelineExt::destroy(loadedRayTracingQueue.front().pipeline);
			loadedRayTracingQueue.pop();
		}
		while (!loadedComputeQueue.empty())
		{
			PipelineExt::destroy(loadedComputeQueue.front().pipeline);
			loadedComputeQueue.pop();
		}
		while (!loadedGraphicsQueue.empty())
		{
			PipelineExt::destroy(loadedGraphicsQueue.front().pipeline);
			loadedGraphicsQueue.pop();
		}
		GraphicsAPI::get()->forceResourceDestroy = false;

		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Input", ResourceSystem::input);
		#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
		ECSM_TRY_UNSUBSCRIBE_FROM_EVENT("FileChange", ResourceSystem::fileChange);
		#endif
	}
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

		if (item.renderPass)
		{
			auto& shareCount = graphicsAPI->renderPasses.at(item.renderPass);
			if (shareCount == 0)
			{
				graphicsAPI->destroyResource(GraphicsAPI::DestroyResourceType::Framebuffer, nullptr, item.renderPass);
				graphicsAPI->renderPasses.erase(item.renderPass);
			}
			else
			{
				shareCount--;
			}
		}

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
void ResourceSystem::dequeueBuffers()
{
	SET_CPU_ZONE_SCOPED("Loaded Buffers Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

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
		auto& item = loadedBufferQueue.front();
		auto buffer = *item.bufferInstance <= graphicsAPI->bufferPool.getOccupancy() ? // Note: getOccupancy() required, do not optimize!
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
void ResourceSystem::dequeueImages()
{
	SET_CPU_ZONE_SCOPED("Loaded Images Dequeue");

	auto graphicsAPI = GraphicsAPI::get();
	auto manager = Manager::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

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
		auto generateMipmap = image->getMipCount() > 1 && !hasAnyFlag(item.flags, ImageLoadFlags::LinearData);
		graphicsSystem->startRecording(generateMipmap ? CommandBufferType::Graphics : CommandBufferType::TransferOnly);
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
	dequeueBuffers();
	dequeueImages();
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
				if (pipeline.getDebugName().find(resourcePath) == string::npos) continue;
				graphicsPipelines.emplace(resourcePath, graphicsPipelinePool.getID(&pipeline));
			}
		}
		else if (isComputeShader)
		{
			auto& computePipelinePool = graphicsAPI->computePipelinePool;
			for (auto& pipeline : computePipelinePool)
			{
				if (pipeline.getDebugName().find(resourcePath) == string::npos) continue;
				computePipelines.emplace(resourcePath, computePipelinePool.getID(&pipeline));
			}
		}
		else if (isRayTracingShader)
		{
			auto& rayTracingPipelinePool = graphicsAPI->rayTracingPipelinePool;
			for (auto& pipeline : rayTracingPipelinePool)
			{
				if (pipeline.getDebugName().find(resourcePath) == string::npos) continue;
				rayTracingPipelines.emplace(resourcePath, rayTracingPipelinePool.getID(&pipeline));
			}
			// TODO: also detect ray tracing shader variants .1., .2. etc.
		}
		else if (isMeshShader)
		{
			// TODO:
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
			string_view resourcePath = gslHeaders.back(); gslHeaders.pop_back();
			collectGslHeaderUsers(GARDEN_RESOURCES_PATH, appCachePath, resourcePath, gslHeaders, 
				checkedPaths, graphicsPipelines, computePipelines, rayTracingPipelines);
			collectGslHeaderUsers(appResourcesPath, appCachePath, resourcePath, gslHeaders, 
				checkedPaths, graphicsPipelines, computePipelines, rayTracingPipelines);
			checkCount++;
		}

		if (checkCount >= 10000)
			GARDEN_LOG_ERROR("Detected shader GSL circular includes!");
	}
	catch (exception& e)
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

			ResourceSystem::GraphicsOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.useAsyncRecording = pipelineView->useAsyncRecording();
			options.loadAsync = false;
			options.subpassIndex = pipelineView->getSubpassIndex();
			// TODO: store and load overrides?

			try
			{
				auto newPipeline = resourceSystem->loadGraphicsPipeline(
					pair.first, pipelineView->getFramebuffer(), options);
				auto newPipelineView = graphicsPipelinePool.get(newPipeline);
				swap(**graphicsPipelinePool.get(pair.second), **newPipelineView);

				// TODO: we need to recreate all descriptor sets to prevent use after free errors!
				// graphicsPipelinePool.destroy(newPipeline);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (exception& e)
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

			ResourceSystem::ComputeOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.useAsyncRecording = pipelineView->useAsyncRecording();
			options.loadAsync = false;

			try
			{
				auto newPipeline = resourceSystem->loadComputePipeline(pair.first, options);
				auto newPipelineView = computePipelinePool.get(newPipeline);
				swap(**computePipelinePool.get(pair.second), **newPipelineView);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (exception& e)
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

			ResourceSystem::RayTracingOptions options;
			options.specConstValues = &specConstValues;
			options.maxBindlessCount = pipelineView->getMaxBindlessCount();
			options.useAsyncRecording = pipelineView->useAsyncRecording();
			options.loadAsync = false;

			try
			{
				auto newPipeline = resourceSystem->loadRayTracingPipeline(pair.first, options);
				auto newPipelineView = rayTracingPipelinePool.get(newPipeline);
				swap(**rayTracingPipelinePool.get(pair.second), **newPipelineView);
				newPipelineView->setDebugName(newPipelineView->getDebugName() + ".old");
			}
			catch (exception& e)
			{
				GARDEN_LOG_ERROR(string(e.what()));
			}
		}
	}
}
#endif

//**********************************************************************************************************************
void ResourceSystem::fileChange()
{
	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto fileWatcherSystem = FileWatcherSystem::Instance::get();
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
	data.resize(sizeof(float4) * 16);
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

//**********************************************************************************************************************
static void loadMissingModel(const vector<BufferChannel>& channels, vector<uint8>& vertexData, 
	vector<uint8>& indexData, uint32& vertexCount, uint32& indexCount)
{
	const float4 colorMagenta = (float4)Color::magenta; const float4 colorBlack = (float4)Color::black;
	auto vertexSize = toBinarySize(channels);
	vertexData.resize(vertexSize * 3);
	auto vertices = vertexData.data();

	for (auto channel : channels)
	{
		switch (channel)
		{
		case BufferChannel::Positions:
			*(float3*)(vertices                 ) = float3(-1.0f, -1.0f, 0.0f);
			*(float3*)(vertices + vertexSize    ) = float3( 1.0f, -1.0f, 0.0f);
			*(float3*)(vertices + vertexSize * 2) = float3( 1.0f,  1.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Normals:
			*(float3*)(vertices                 ) = float3(0.0f, 0.0f, -1.0f);
			*(float3*)(vertices + vertexSize    ) = float3(0.0f, 0.0f, -1.0f);
			*(float3*)(vertices + vertexSize * 2) = float3(0.0f, 0.0f, -1.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Tangents:
			*(float3*)(vertices                 ) = float3(1.0f, 0.0f, 0.0f);
			*(float3*)(vertices + vertexSize    ) = float3(1.0f, 0.0f, 0.0f);
			*(float3*)(vertices + vertexSize * 2) = float3(1.0f, 0.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::Bitangents:
			*(float3*)(vertices                 ) = float3(0.0f, 1.0f, 0.0f);
			*(float3*)(vertices + vertexSize    ) = float3(0.0f, 1.0f, 0.0f);
			*(float3*)(vertices + vertexSize * 2) = float3(0.0f, 1.0f, 0.0f);
			vertices += sizeof(float3);
			break;
		case BufferChannel::TextureCoords:
			*(float2*)(vertices                 ) = float2(0.0f, 0.0f);
			*(float2*)(vertices + vertexSize    ) = float2(1.0f, 0.0f);
			*(float2*)(vertices + vertexSize * 2) = float2(0.5f, 1.0f);
			vertices += sizeof(float2);
			break;
		case BufferChannel::VertexColors:
			*(float4*)(vertices                 ) = colorBlack;
			*(float4*)(vertices + vertexSize    ) = colorBlack;
			*(float4*)(vertices + vertexSize * 2) = colorMagenta;
			vertices += sizeof(float4);
			break;
		default: abort();
		}
	}
}

#if !GARDEN_PACK_RESOURCES
//**********************************************************************************************************************
static int32 getImageFilePath(const fs::path& appResourcesPath,
	fs::path imagePath, fs::path& filePath, ImageFileType& fileType)
{
	int32 fileCount = 0;
	imagePath += ".ext";

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
static int32 getCacheImageFilePath(const fs::path& appCachePath,
	fs::path imagePath, fs::path& filePath, ImageFileType& fileType)
{
	int32 fileCount = 0;
	imagePath = appCachePath / fs::path("images") / imagePath;
	imagePath += ".ext";

	for (uint8 i = 0; i < (uint8)ResourceSystem::imageFileExts.size(); i++)
	{
		imagePath.replace_extension(ResourceSystem::imageFileExts[i]);
		if (fs::exists(imagePath))
		{
			filePath = imagePath;
			fileType = ResourceSystem::imageFileTypes[i];
			fileCount++;
		}
	}

	return fileCount;
}
static int32 getImageFilePath(const fs::path& appCachePath, const fs::path& appResourcesPath,
	const fs::path& imagePath, fs::path& filePath, ImageFileType& fileType)
{
	auto fileCount = getCacheImageFilePath(appCachePath, imagePath, filePath, fileType);
	if (fileCount > 0)
		return fileCount;

	fileCount += getImageFilePath(appResourcesPath, fs::path("images") / imagePath, filePath, fileType);
	fileCount += getImageFilePath(appResourcesPath, fs::path("models") / imagePath, filePath, fileType);
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
	imagePath += ".ext";

	uint64 itemIndex = 0;
	for (uint8 i = 0; i < (uint8)imageFileExts.size(); i++)
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
	auto fileCount = getImageFilePath(appCachePath, appResourcesPath, path, filePath, fileType);

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
	auto pixelCount = equiData.size() / sizeof(float4);

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
			threadPool.addItems([srcData, dstData](const ThreadPool::Task& task)
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
	auto pixelsSize = sizeof(float4) * cubemapSize * cubemapSize;
	left.resize(pixelsSize); right.resize(pixelsSize);
	bottom.resize(pixelsSize); top.resize(pixelsSize);
	back.resize(pixelsSize); front.resize(pixelsSize);

	f32x4* cubeFaces[Image::cubemapFaceCount] =
	{
		(f32x4*)left.data(), (f32x4*)right.data(),
		(f32x4*)bottom.data(), (f32x4*)top.data(),
		(f32x4*)back.data(), (f32x4*)front.data(),
	};

	if (threadIndex < 0 && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([=](const ThreadPool::Task& task)
		{
			auto layerSize = cubemapSize * cubemapSize;
			auto itemCount = task.getItemCount();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto index = i; uint3 coords;
				coords.z = index / layerSize;
				index %= layerSize;
				coords.y = index / cubemapSize;
				coords.x = index % cubemapSize;
				auto cubePixels = cubeFaces[coords.z];

				Equi2Cube::convert(coords, cubemapSize, equiSize,
					equiSizeMinus1, equiPixels, cubePixels, invDim);
			}
		},
		cubemapSize * cubemapSize * Image::cubemapFaceCount);
		threadPool.wait();
	}
	else
	{
		for (uint32 face = 0; face < Image::cubemapFaceCount; face++)
		{
			auto cubePixels = cubeFaces[face];
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
	Image::Format format;
	
	#if !GARDEN_PACK_RESOURCES
	auto cacheFilePath = appCachePath / "images" / path;
	auto cacheFileString = cacheFilePath.generic_string();

	fs::path inputFilePath; ImageFileType inputFileType; 
	auto fileCount = getImageFilePath(appCachePath, appResourcesPath, path, inputFilePath, inputFileType);

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
	if (!fs::exists(cacheFileString + "-nx.exr") || !fs::exists(cacheFileString + "-px.exr") ||
		!fs::exists(cacheFileString + "-ny.exr") || !fs::exists(cacheFileString + "-py.exr") ||
		!fs::exists(cacheFileString + "-nz.exr") || !fs::exists(cacheFileString + "-pz.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-nx.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-px.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-ny.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-py.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-nz.exr") ||
		inputLastWriteTime > fs::last_write_time(cacheFileString + "-pz.exr"))
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

		fs::create_directories(cacheFilePath.parent_path());
		if (threadIndex < 0 && threadSystem)
		{
			auto& threadPool = threadSystem->getForegroundPool();
			threadPool.addTasks([&](const ThreadPool::Task& task)
			{
				switch (task.getTaskIndex())
				{
					case 0: writeExrImageData(cacheFileString + "-nx.exr", cubemapSize, left); break;
					case 1: writeExrImageData(cacheFileString + "-px.exr", cubemapSize, right); break;
					case 2: writeExrImageData(cacheFileString + "-ny.exr", cubemapSize, bottom); break;
					case 3: writeExrImageData(cacheFileString + "-py.exr", cubemapSize, top); break;
					case 4: writeExrImageData(cacheFileString + "-nz.exr", cubemapSize, back); break;
					case 5: writeExrImageData(cacheFileString + "-pz.exr", cubemapSize, front); break;
					default: abort();
				}
			}, Image::cubemapFaceCount);
			threadPool.wait();
		}
		else
		{
			writeExrImageData(cacheFileString + "-nx.exr", cubemapSize, left);
			writeExrImageData(cacheFileString + "-px.exr", cubemapSize, right);
			writeExrImageData(cacheFileString + "-ny.exr", cubemapSize, bottom);
			writeExrImageData(cacheFileString + "-py.exr", cubemapSize, top);
			writeExrImageData(cacheFileString + "-nz.exr", cubemapSize, back);
			writeExrImageData(cacheFileString + "-pz.exr", cubemapSize, front);
		}

		GARDEN_LOG_DEBUG("Converted spherical cubemap. (path: " + path.generic_string() + ")");
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
		}, Image::cubemapFaceCount);
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
		GARDEN_LOG_ERROR("Invalid cubemap face size. (path: " + path.generic_string() + ")");
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
		fileType == ImageFileType::Psd || fileType == ImageFileType::Tga ||
		fileType == ImageFileType::Pic || fileType == ImageFileType::Gif)
	{
		int sizeX = 0, sizeY = 0;
		auto pixelData = stbi_load_from_memory(data,
			(int)dataSize, &sizeX, &sizeY, nullptr, 4);
		if (!pixelData)
			throw GardenError("Invalid JPG image data.");

		imageSize = uint2(sizeX, sizeY);
		pixels.assign(pixelData, pixelData + sizeof(Color) * imageSize.x * imageSize.y);
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
		pixels.assign((const uint8*)pixelData, (const uint8*)pixelData +
			sizeof(float4) * imageSize.x * imageSize.y);
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
		pixels.assign((const uint8*)pixelData, (const uint8*)pixelData +
			sizeof(float4) * imageSize.x * imageSize.y);
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
	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::Load3D) && realSize.x > realSize.y)
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

	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::Load3D))
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
static Image::Type calcLoadedImageType(uint32 sizeY, ImageLoadFlags flags) noexcept
{
	if (hasAnyFlag(flags, ImageLoadFlags::TypeCubemap))
		return Image::Type::Cubemap;
	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::TypeArray))
		return sizeY == 1 ? Image::Type::Texture1DArray : Image::Type::Texture2DArray;
	if (hasAnyFlag(flags, ImageLoadFlags::Load3D | ImageLoadFlags::Type3D))
		return Image::Type::Texture3D;
	return sizeY == 1 ? Image::Type::Texture1D : Image::Type::Texture2D;
}
static uint8 calcLoadedImageMipCount(uint8 maxMipCount, uint2 imageSize) noexcept
{
	return maxMipCount == 0 ? calcMipCount(imageSize) : std::min(maxMipCount, calcMipCount(imageSize));
}

//**********************************************************************************************************************
Ref<Image> ResourceSystem::loadImageArray(const vector<fs::path>& paths, Image::Usage usage,
	uint8 maxMipCount, Image::Strategy strategy, ImageLoadFlags flags, float taskPriority)
{
	// TODO: allow to load file with image paths to load image arrays.
	GARDEN_ASSERT(!paths.empty());
	GARDEN_ASSERT(hasAnyFlag(usage, Image::Usage::TransferDst));
	GARDEN_ASSERT(hasAnyFlag(usage, Image::Usage::TransferQ));

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (paths.size() > 1)
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::Load3D));
	}
	else
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::TypeCubemap));
	}
	if (hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::TypeArray))
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::Load3D | 
			ImageLoadFlags::Type3D | ImageLoadFlags::TypeCubemap));
	}
	if (hasAnyFlag(flags, ImageLoadFlags::Load3D | ImageLoadFlags::Type3D))
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::LoadArray | 
			ImageLoadFlags::TypeArray | ImageLoadFlags::TypeCubemap));
	}
	if (hasAnyFlag(flags, ImageLoadFlags::TypeCubemap))
	{
		GARDEN_ASSERT(!hasAnyFlag(flags, ImageLoadFlags::LoadArray | ImageLoadFlags::TypeArray | 
			ImageLoadFlags::Load3D | ImageLoadFlags::Type3D));
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

		Hash128::updateState(hashState, &usage, sizeof(Image::Usage));
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
	auto imageVersion = graphicsAPI->imageVersion++;
	auto image = graphicsAPI->imagePool.create(usage, strategy, imageVersion);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto resource = graphicsAPI->imagePool.get(image);
	if (paths.size() > 1)
	{
		if (hasAnyFlag(flags, ImageLoadFlags::LoadArray))
			resource->setDebugName("imageArray." + debugName + paths[0].generic_string());
		else resource->setDebugName("image3D." + debugName + paths[0].generic_string());
	}
	else resource->setDebugName("image." + debugName + paths[0].generic_string());
	#endif

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (!hasAnyFlag(flags, ImageLoadFlags::LoadSync) && threadSystem)
	{
		auto data = new ImageLoadData();
		data->imageVersion = imageVersion;
		data->paths = paths;
		data->instance = image;
		data->usage = usage;
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
			auto mipCount = calcLoadedImageMipCount(data->maxMipCount, imageSize);
			auto type = calcLoadedImageType(realSize.y, data->flags);
			
			ImageQueueItem item =
			{
				ImageExt::create(type, format, data->usage, data->strategy, 
					u32x4(imageSize.x, imageSize.y, layerCount, mipCount), data->imageVersion),
				BufferExt::create(Buffer::Usage::TransferSrc, Buffer::CpuAccess::SequentialWrite, 
					Buffer::Location::Auto, Buffer::Strategy::Speed, // Note: Staging does not need TransferQ flag.
					formatBinarySize * realSize.x * realSize.y * paths.size(), 0),
				std::move(paths), realSize, data->instance, data->flags
			};

			copyLoadedImageData(pixelArrays, item.staging.getMap(),
				realSize, imageSize, formatBinarySize, data->flags);
			item.staging.flush();

			queueLocker.lock();
			loadedImageQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		taskPriority);
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
		auto mipCount = calcLoadedImageMipCount(maxMipCount, imageSize);
		auto type = calcLoadedImageType(realSize.y, flags);

		auto imageInstance = ImageExt::create(type, format, usage, strategy,
			u32x4(imageSize.x, imageSize.y, layerCount, mipCount), 0);
		auto imageView = graphicsAPI->imagePool.get(image);
		ImageExt::moveInternalObjects(imageInstance, **imageView);

		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto stagingBuffer =  graphicsSystem->createStagingBuffer(
			Buffer::CpuAccess::SequentialWrite, formatBinarySize * realSize.x * realSize.y);
		SET_RESOURCE_DEBUG_NAME(stagingBuffer, "buffer.staging.loadedImage" + to_string(*stagingBuffer));

		auto stagingView = graphicsAPI->bufferPool.get(stagingBuffer);
		copyLoadedImageData(pixelArrays, stagingView->getMap(),
			realSize, imageSize, formatBinarySize, flags);
		stagingView->flush();

		auto generateMipmap = imageView->getMipCount() > 1 && !hasAnyFlag(flags, ImageLoadFlags::LinearData);
		graphicsSystem->startRecording(generateMipmap ? CommandBufferType::Graphics : CommandBufferType::TransferOnly);
		Image::copy(stagingBuffer, image);
		if (generateMipmap) imageView->generateMips();
		graphicsSystem->stopRecording();
		graphicsAPI->bufferPool.destroy(stagingBuffer);

		LoadedImageItem item;
		item.paths = paths;
		item.instance = image;
		loadedImageArray.push_back(std::move(item));
	}

	auto imageRef = Ref<Image>(image);
	if (hasAnyFlag(flags, ImageLoadFlags::LoadShared))
	{
		auto result = sharedImages.emplace(hash, imageRef);
		GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
	}

	return imageRef;
}

//**********************************************************************************************************************
void ResourceSystem::storeImage(const fs::path& path, const void* data, 
	uint2 size, float quality, ImageFileType fileType)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(areAllTrue(size > uint2::zero));
	GARDEN_ASSERT(quality >= 0.0f && quality <= 1.0f);

	if (!fs::exists(path.parent_path()))
		fs::create_directories(path.parent_path());

	if (fileType == ImageFileType::Webp)
	{
		auto filePath = path;
		filePath.replace_extension(imageFileExts[(psize)ImageFileType::Webp]);
		ofstream outputStream(path, ios::binary | ios::trunc);
		if (!outputStream.is_open())
			throw GardenError("Failed to store image file. (path: " + path.generic_string() + ")");
		outputStream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

		uint8_t* encoded = nullptr; size_t encodedSize = 0;
		if (quality == 1.0f)
			encodedSize = WebPEncodeLosslessRGBA((const uint8_t*)data, size.x, size.y, size.x * 4, &encoded);
		else
			encodedSize = WebPEncodeRGBA((const uint8_t*)data, size.x, size.y, size.x * 4, quality * 100.0f, &encoded);
		WebPFree(encoded);

		if (encodedSize == 0)
			throw GardenError("Failed to encode WebP image. (path: " + path.generic_string() + ")");
		outputStream.write((const char*)encoded, encodedSize);
	}
	else if (fileType == ImageFileType::Png)
	{
		png_image image;
		memset(&image, 0, (sizeof image));
		image.version = PNG_IMAGE_VERSION;
		image.width = size.x;
		image.height = size.y;
		image.format = PNG_FORMAT_RGBA;

		if (!png_image_write_to_file(&image, path.generic_string().c_str(), 0, data, 0, NULL))
			throw GardenError("Failed to write PNG image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == ImageFileType::Jpg)
	{
		if (!stbi_write_jpg(path.generic_string().c_str(), size.x, size.y, 4, data, quality * 100.0f))
			throw GardenError("Failed to write JPG image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == ImageFileType::Exr)
	{
		abort(); // TODO: implement: https://github.com/syoyo/tinyexr
	}
	else if (fileType == ImageFileType::Hdr)
	{
		if (!stbi_write_hdr(path.generic_string().c_str(), size.x, size.y, 4, (const float*)data))
			throw GardenError("Failed to write HDR image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == ImageFileType::Bmp)
	{
		if (!stbi_write_bmp(path.generic_string().c_str(), size.x, size.y, 4, data))
			throw GardenError("Failed to write BMP image. (path: " + path.generic_string() + ")");
	}
	else if (fileType == ImageFileType::Tga)
	{
		if (!stbi_write_tga(path.generic_string().c_str(), size.x, size.y, 4, data))
			throw GardenError("Failed to write TGA image. (path: " + path.generic_string() + ")");
	}
	else abort();
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

	GraphicsSystem::Instance::get()->destroy(image);
}

//**********************************************************************************************************************
Ref<Buffer> ResourceSystem::loadBuffer(const vector<fs::path>& path, 
	Buffer::Strategy strategy, BufferLoadFlags flags, float taskPriority)
{
	GARDEN_ASSERT(!path.empty());
	abort(); // TODO: load plain buffer binary data.
}
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

	GraphicsSystem::Instance::get()->destroy(buffer);
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

	auto graphicsSystem = GraphicsSystem::Instance::get();
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

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto descriptorSet = graphicsSystem->createDescriptorSet(computePipeline, std::move(uniforms), {}, index);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + hash.toBase64URL());

	auto sharedDescriptorSet = Ref<DescriptorSet>(descriptorSet);
	auto result = sharedDescriptorSets.emplace(hash, sharedDescriptorSet);
	GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
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

	GraphicsSystem::Instance::get()->destroy(descriptorSet);
}

//**********************************************************************************************************************
static bool loadOrCompileGraphics(GslCompiler::GraphicsData& data)
{
	#if !GARDEN_PACK_RESOURCES
	auto shadersPath = "shaders" / data.shaderPath;
	auto headerPath = shadersPath; headerPath += ".gslh";
	auto vertexPath = shadersPath; vertexPath += ".vert";
	auto fragmentPath = shadersPath; fragmentPath += ".frag";

	fs::path vertexInputPath, fragmentInputPath;
	auto hasVertexShader = File::tryGetResourcePath(data.resourcesPath, vertexPath, vertexInputPath);
	auto hasFragmentShader = File::tryGetResourcePath(data.resourcesPath, fragmentPath, fragmentInputPath);

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
	
	if (!fs::exists(headerFilePath) ||
		(hasVertexShader && (!fs::exists(vertexOutputPath) ||
		fs::last_write_time(vertexInputPath) > fs::last_write_time(vertexOutputPath))) ||
		(hasFragmentShader && (!fs::exists(fragmentOutputPath) ||
		fs::last_write_time(fragmentInputPath) > fs::last_write_time(fragmentOutputPath))))
	{
		const vector<fs::path> includePaths =
		{ GARDEN_RESOURCES_PATH / "shaders", data.resourcesPath / "shaders" };
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
		GARDEN_LOG_ERROR("Failed to load graphics shaders. ("
			"name: " + data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<GraphicsPipeline> ResourceSystem::loadGraphicsPipeline(const fs::path& path,
	ID<Framebuffer> framebuffer, const GraphicsOptions& options)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(framebuffer, "Assert " + path.generic_string());
	// TODO: validate specConstValues and stateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto framebufferView = graphicsAPI->framebufferPool.get(framebuffer);
	const auto& subpasses = framebufferView->getSubpasses();

	GARDEN_ASSERT_MSG((subpasses.empty() && options.subpassIndex == 0) || (!subpasses.empty() && 
		options.subpassIndex < subpasses.size()), "Assert " + path.generic_string());

	auto pipelineVersion = graphicsAPI->graphicsPipelineVersion++;
	auto pipeline = graphicsAPI->graphicsPipelinePool.create(path, options.maxBindlessCount, 
		options.useAsyncRecording, pipelineVersion, framebuffer, options.subpassIndex);

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
		colorFormats.reserve(colorAttachments.size());
		for (uint32 i = 0; i < (uint32)colorAttachments.size(); i++)
		{
			if (!colorAttachments[i].imageView)
			{
				colorFormats.push_back(Image::Format::Undefined);
				continue;
			}
			
			auto attachment = graphicsAPI->imageViewPool.get(colorAttachments[i].imageView);
			colorFormats.push_back(attachment->getFormat());
		}
	}
	else
	{
		const auto& outputAttachments = subpasses[options.subpassIndex].outputAttachments;
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
	if (options.loadAsync && threadSystem && !options.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!options.shaderOverrides, "Nothing to load asynchronously");
		auto data = new GraphicsPipelineLoadData();
		data->shaderPath = path;
		data->pipelineVersion = pipelineVersion;
		data->renderPass = renderPass;
		data->subpassIndex = options.subpassIndex;
		data->colorFormats = std::move(colorFormats);
		if (options.specConstValues)
			data->specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*options.samplerStateOverrides);
		if (options.pipelineStateOverrides)
			data->pipelineStateOverrides = std::move(*options.pipelineStateOverrides);
		if (options.blendStateOverrides)
			data->blendStateOverrides = std::move(*options.blendStateOverrides);
		data->instance = pipeline;
		data->maxBindlessCount = options.maxBindlessCount;
		data->depthStencilFormat = depthStencilFormat;
		data->subpassIndex = options.subpassIndex;
		data->useAsyncRecording = options.useAsyncRecording;
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
			pipelineData.renderPass = data->renderPass;
			pipelineData.subpassIndex = data->subpassIndex;
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
				GraphicsPipelineExt::create(pipelineData, data->useAsyncRecording),
				data->renderPass, data->instance
			};

			queueLocker.lock();
			loadedGraphicsQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		options.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Graphics Pipeline Load");

		GslCompiler::GraphicsData pipelineData;
		if (options.specConstValues)
			pipelineData.specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*options.samplerStateOverrides);
		if (options.pipelineStateOverrides)
			pipelineData.pipelineStateOverrides = std::move(*options.pipelineStateOverrides);
		if (options.blendStateOverrides)
			pipelineData.blendStateOverrides = std::move(*options.blendStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = options.maxBindlessCount;
		pipelineData.colorFormats = std::move(colorFormats);
		pipelineData.renderPass = renderPass;
		pipelineData.subpassIndex = options.subpassIndex;
		pipelineData.depthStencilFormat = depthStencilFormat;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif

		if (options.shaderOverrides)
		{
			pipelineData.headerData = std::move(options.shaderOverrides->headerData);
			pipelineData.vertexCode = std::move(options.shaderOverrides->vertexCode);
			pipelineData.fragmentCode = std::move(options.shaderOverrides->fragmentCode);
			GslCompiler::loadGraphicsShaders(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileGraphics(pipelineData))
			{
				graphicsAPI->graphicsPipelinePool.destroy(pipeline);
				throw GardenError("Failed to load graphics pipeline. ("
					"path: " + path.generic_string() + ")");
			}
		}
			
		auto graphicsPipeline = GraphicsPipelineExt::create(pipelineData, options.useAsyncRecording);
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
	if (!File::tryGetResourcePath(data.resourcesPath, computePath, computeInputPath))
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
		{ GARDEN_RESOURCES_PATH / "shaders", data.resourcesPath / "shaders" };
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
		GARDEN_LOG_ERROR("Failed to load compute shader. ("
			"name: " + data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<ComputePipeline> ResourceSystem::loadComputePipeline(const fs::path& path, const ComputeOptions& options)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineVersion = graphicsAPI->computePipelineVersion++;
	auto pipeline = graphicsAPI->computePipelinePool.create(path, 
		options.maxBindlessCount, options.useAsyncRecording, pipelineVersion);

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (options.loadAsync && threadSystem && !options.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!options.shaderOverrides, "Nothing to load asynchronously");
		auto data = new ComputePipelineLoadData();
		data->pipelineVersion = pipelineVersion;
		data->shaderPath = path;
		if (options.specConstValues)
			data->specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*options.samplerStateOverrides);
		data->maxBindlessCount = options.maxBindlessCount;
		data->instance = pipeline;
		data->useAsyncRecording = options.useAsyncRecording;
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
				ComputePipelineExt::create(pipelineData, data->useAsyncRecording),
				data->instance
			};

			queueLocker.lock();
			loadedComputeQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		options.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Compute Pipeline Load");

		GslCompiler::ComputeData pipelineData;
		
		if (options.specConstValues)
			pipelineData.specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*options.samplerStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = options.maxBindlessCount;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif
		
		if (options.shaderOverrides)
		{
			pipelineData.headerData = std::move(options.shaderOverrides->headerData);
			pipelineData.code = std::move(options.shaderOverrides->code);
			GslCompiler::loadComputeShader(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileCompute(pipelineData))
			{
				graphicsAPI->computePipelinePool.destroy(pipeline);
				throw GardenError("Failed to load compute pipeline. ("
					"path: " + path.generic_string() + ")");
			}
		}

		auto computePipeline = ComputePipelineExt::create(pipelineData, options.useAsyncRecording);
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
	auto hasRayGenShader = File::tryGetResourcePath(data.resourcesPath, rayGenerationPath, rayGenInputPath);
	auto hasMissShader = File::tryGetResourcePath(data.resourcesPath, missPath, missInputPath);
	auto hasCallableShader = File::tryGetResourcePath(data.resourcesPath, callablePath, callInputPath);
	auto hasIntersectShader = File::tryGetResourcePath(data.resourcesPath, intersectionPath, intersectInputPath);
	auto hasAnyHitShader = File::tryGetResourcePath(data.resourcesPath, anyHitPath, anyHitInputPath);
	auto hasClosHitShader = File::tryGetResourcePath(data.resourcesPath, closestHitPath, closHitInputPath);

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

	// !!! TODO: chec for changes in additional ray tracing shader hit groups shaders. rt-shader.1.rchit.spv

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
		{ GARDEN_RESOURCES_PATH / "shaders", data.resourcesPath / "shaders" };
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
		GARDEN_LOG_ERROR("Failed to load ray tracing shaders. ("
			"name: " + data.shaderPath.generic_string() + ", error: " + string(e.what()) + ")");
		return false;
	}

	return true;
}

//**********************************************************************************************************************
ID<RayTracingPipeline> ResourceSystem::loadRayTracingPipeline(const fs::path& path, const RayTracingOptions& options)
{
	GARDEN_ASSERT(!path.empty());
	// TODO: validate specConstValues and samplerStateOverrides

	auto graphicsAPI = GraphicsAPI::get();
	auto pipelineVersion = graphicsAPI->rayTracingPipelineVersion++;
	auto pipeline = graphicsAPI->rayTracingPipelinePool.create(path, 
		options.maxBindlessCount, options.useAsyncRecording, pipelineVersion);

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (options.loadAsync && threadSystem && !options.shaderOverrides)
	{
		GARDEN_ASSERT_MSG(!options.shaderOverrides, "Nothing to load asynchronously");
		auto data = new RayTracingPipelineLoadData();
		data->pipelineVersion = pipelineVersion;
		data->shaderPath = path;
		if (options.specConstValues)
			data->specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			data->samplerStateOverrides = std::move(*options.samplerStateOverrides);
		data->maxBindlessCount = options.maxBindlessCount;
		data->instance = pipeline;
		data->useAsyncRecording = options.useAsyncRecording;
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
				RayTracingPipelineExt::create(pipelineData, data->useAsyncRecording),
				data->instance
			};

			queueLocker.lock();
			loadedRayTracingQueue.push(std::move(item));
			queueLocker.unlock();

			delete data;
		},
		options.taskPriority);
	}
	else
	{
		SET_CPU_ZONE_SCOPED("Ray Tracing Pipeline Load");

		GslCompiler::RayTracingData pipelineData;
		
		if (options.specConstValues)
			pipelineData.specConstValues = std::move(*options.specConstValues);
		if (options.samplerStateOverrides)
			pipelineData.samplerStateOverrides = std::move(*options.samplerStateOverrides);
		pipelineData.pipelineVersion = pipelineVersion;
		pipelineData.maxBindlessCount = options.maxBindlessCount;
		#if GARDEN_PACK_RESOURCES
		pipelineData.packReader = &packReader;
		pipelineData.threadIndex = -1;
		#else
		pipelineData.resourcesPath = appResourcesPath;
		pipelineData.cachePath = appCachePath;
		#endif
		
		if (options.shaderOverrides)
		{
			pipelineData.headerData = std::move(options.shaderOverrides->headerData);
			pipelineData.rayGenGroups = std::move(options.shaderOverrides->rayGenGroups);
			pipelineData.missGroups = std::move(options.shaderOverrides->missGroups);
			pipelineData.callGroups = std::move(options.shaderOverrides->callGroups);
			pipelineData.hitGroups = std::move(options.shaderOverrides->hitGroups);
			GslCompiler::loadRayTracingShaders(pipelineData);
		}
		else
		{
			pipelineData.shaderPath = path;
			if (!loadOrCompileRayTracing(pipelineData))
			{
				graphicsAPI->rayTracingPipelinePool.destroy(pipeline);
				throw GardenError("Failed to load ray tracing pipeline. ("
					"path: " + path.generic_string() + ")");
			}
		}

		auto rayTracingPipeline = RayTracingPipelineExt::create(pipelineData, options.useAsyncRecording);
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

	auto manager = Manager::Instance::get();
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
	catch (exception& e)
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
	catch (exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize scene. (path: " + 
			path.generic_string() + ", error: " + string(e.what()) + ")");
		return {};
	}
	#endif

	ID<Entity> rootEntity = {};
	if (addRootEntity)
	{
		if (TransformSystem::Instance::has())
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

void ResourceSystem::clearScene()
{
	auto manager = Manager::Instance::get();
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
void ResourceSystem::storeScene(const fs::path& path, ID<Entity> rootEntity, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());

	auto manager = Manager::Instance::get();
	auto systemGroup = manager->tryGetSystemGroup<ISerializable>();
	if (!systemGroup)
	{
		GARDEN_LOG_ERROR("No ISerializable system found.");
		return;
	}

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

	for (auto system : *systemGroup)
	{
		auto serializableSystem = dynamic_cast<ISerializable*>(system);
		serializableSystem->preSerialize(serializer);
	}

	serializer.write("version", appVersion.toString3());
	serializer.beginChild("entities");
	
	const auto& entities = manager->getEntities();
	auto dnsSystem = DoNotSerializeSystem::Instance::tryGet();
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
				childEntities.push(childs[i]);
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
Ref<Animation> ResourceSystem::loadAnimation(const fs::path& path, bool loadShared)
{
	GARDEN_ASSERT(!path.empty());
	
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
	catch (exception& e)
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
	catch (exception& e)
	{
		GARDEN_LOG_ERROR("Failed to deserialize animation. (path: " + 
			path.generic_string() + ", error: " + string(e.what()) + ")");
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

	auto animationRef = Ref<Animation>(animation);
	if (loadShared)
	{
		auto result = sharedAnimations.emplace(hash, animationRef);
		GARDEN_ASSERT_MSG(result.second, "Detected memory corruption");
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

	AnimationSystem::Instance::get()->destroy(animation);
}

//**********************************************************************************************************************
void ResourceSystem::storeAnimation(const fs::path& path, ID<Animation> animation, const fs::path& directory)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT_MSG(animation, "Assert " + path.generic_string());

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

	auto textSystem = TextSystem::Instance::get();
	fs::path filePath = "fonts" / path; filePath += ".ttf";

	#if GARDEN_PACK_RESOURCES
	uint64 itemIndex = 0;
	if (!packReader.getItemIndex(filePath, itemIndex))
	{
		if (logMissing)
			GARDEN_LOG_ERROR("Font does not exist. (path: " + path.generic_string() + ")");
		return {};
	}

	auto dataSize = packReader.getItemDataSize(itemIndex);
	auto data = new uint8[dataSize];
	packReader.readItemData(itemIndex, data);
	#else
	fs::path fontPath;
	if (!File::tryGetResourcePath(appResourcesPath, filePath, fontPath))
	{
		if (logMissing)
			GARDEN_LOG_ERROR("Font file does not exist or ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	auto dataSize = File::getFileSize(fontPath);
	auto data = new uint8[dataSize];
	File::loadBinary(fontPath, data, dataSize);
	#endif

	auto ftLibrary = (FT_Library)textSystem->ftLibrary;
	auto threadSystem = ThreadSystem::Instance::tryGet();
	vector<void*> faces(threadSystem ? threadSystem->getForegroundPool().getThreadCount() : 1);

	for (auto& face : faces)
	{
		FT_Face ftFace = nullptr;
		auto result = FT_New_Memory_Face(ftLibrary, data, dataSize, faceIndex, &ftFace);
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
			delete[] data;
			return {};
		}
		face = ftFace;
	}

	auto font = Ref<Font>(textSystem->fonts.create());
	auto emplaceResult = sharedFonts.emplace(hash, font);
	GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");

	auto fontView = textSystem->fonts.get(font);
	fontView->faces = std::move(faces);
	fontView->data = data;

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

void ResourceSystem::destroyShared(const Ref<Font>& font)
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

	TextSystem::Instance::get()->fonts.destroy(ID<Font>(font));
}
void ResourceSystem::destroyShared(const FontArray& fonts)
{
	for (const auto& variants : fonts)
	{
		for (const auto& font : variants)
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
	File::loadBinary(resourcePath, data);
	#endif
	return true;
}
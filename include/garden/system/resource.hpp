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

/***********************************************************************************************************************
 * @file Application resource loading functions. (images, models, shaders, pipelines, scenes, sounds, etc.)
 */

#pragma once
#include "garden/resource/image.hpp"
#include "garden/system/graphics.hpp"
#include <queue>

#if !GARDEN_DEBUG
#include "pack/reader.hpp"
#endif

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace garden::graphics;

/***********************************************************************************************************************
 * @brief Game or application resource loader. (images, models, shader, scenes, sounds, etc.)
 * 
 * @details
 * Manages the process of loading, and also the organization of various game assets or resources such as 
 * images or textures, models, shaders, audio or sound files, scenes and other data that games need to run.
 * 
 * Registers events: ImageLoaded, BufferLoaded.
 */
class ResourceSystem : public System
{
protected:
	struct GraphicsQueueItem
	{
		void* renderPass = nullptr;
		GraphicsPipeline pipeline;
		ID<GraphicsPipeline> instance = {};
	};
	struct ComputeQueueItem
	{
		ComputePipeline pipeline;
		ID<ComputePipeline> instance = {};
	};
	struct BufferQueueItem
	{
		Buffer buffer;
		Buffer staging;
		ID<Buffer> instance = {};
	};
	struct ImageQueueItem
	{
		Image image;
		Buffer staging;
		ID<Image> instance = {};
	};
	
	queue<GraphicsQueueItem> graphicsQueue; // TODO: We can use here lock free concurrent queue.
	queue<ComputeQueueItem> computeQueue;
	queue<BufferQueueItem> bufferQueue;
	queue<ImageQueueItem> imageQueue;
	mutex queueLocker;
	ID<Buffer> loadedBuffer = {};
	ID<Image> loadedImage = {};

	#if GARDEN_DEBUG
	fs::path appResourcesPath;
	fs::path appCachesPath;
	Version appVersion;
	#else
	pack::Reader packReader;
	#endif

	static ResourceSystem* instance;

	/**
	 * @brief Creates a new resource system instance.
	 */
	ResourceSystem();
	/**
	 * @brief Destroys resource system instance.
	 */
	~ResourceSystem() override;

	void dequeuePipelines();
	void dequeueBuffersAndImages();

	void init();
	void deinit();
	void input();
	
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Loads image data (pixels) from the resource pack.
	 * 
	 * @param[in] path target image path
	 * @param[out] data image pixel data container
	 * @param[out] size loaded image size in pixels
	 * @param[out] format loaded image data format
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void loadImageData(const fs::path& path, vector<uint8>& data,
		int2& size, Image::Format& format, int32 taskIndex = -1) const;

	/**
	 * @brief Loads cubemap image data (pixels) the resource pack.
	 * 
	 * @param[in] path target cubemap image path
	 * @param[out] left left cubemap image pixel data container
	 * @param[out] right right cubemap image pixel data container
	 * @param[out] bottom bottom cubemap image pixel data container
	 * @param[out] top top cubemap image pixel data container
	 * @param[out] back back cubemap image pixel data container
	 * @param[out] front front cubemap image pixel data container
	 * @param[out] size loaded cubemap image size in pixels
	 * @param[out] format loaded cubemap image data format
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void loadCubemapData(const fs::path& path, vector<uint8>& left, vector<uint8>& right,
		vector<uint8>& bottom, vector<uint8>& top, vector<uint8>& back, vector<uint8>& front,
		int2& size, Image::Format& format, int32 taskIndex = -1) const;
	
	/**
	 * @brief Loads image data (pixels) from the memory file.
	 * 
	 * @param[in] data image data file
	 * @param dataSize image data file size in bytes
	 * @param fileType image data file type
	 * @param[out] pixels image pixel data container
	 * @param[out] imageSize loaded image size in pixels
	 * @param[out] format loaded image data format
	 */
	static void loadImageData(const uint8* data, psize dataSize, ImageFileType fileType,
		vector<uint8>& pixels, int2& imageSize, Image::Format& format);

	/**
	 * @brief Loads image from the resource pack.
	 * 
	 * @param[in] path target image path
	 * @param bind image bind type
	 * @param maxMipCount maximum mipmap level count (0 = unlimited)
	 * @param downscaleCount downscaling iteration count (0 = no downsacling)
	 * @param strategy image memory allocation strategy
	 * @param linearData is image data color space linear
	 * @param loadAsync load image asynchronously without blocking
	 */
	Ref<Image> loadImage(const fs::path& path, Image::Bind bind, uint8 maxMipCount = 1, uint8 downscaleCount = 0,
		Image::Strategy strategy = Buffer::Strategy::Default, bool linearData = false, bool loadAsync = true);

	/**
	 * @brief Returns current loaded image isntance.
	 * @details Useful inside "ImageLoaded" event.
	 */
	ID<Image> getLoadedImage() const noexcept { return loadedImage; }

	/*******************************************************************************************************************
	 * @brief Loads graphics pipeline from the resource pack shaders.
	 * 
	 * @param[in] path target graphics pipeline path
	 * @param framebuffer parent pipeline framebuffer
	 * @param useAsyncRecording can be used for multithreaded commands recording
	 * @param loadAsync load pipeline asynchronously without blocking
	 * @param subpassIndex framebuffer subpass index
	 * @param maxBindlessCount maximum bindless descriptor count
	 * @param[in] specConsts specialization constants array or empty
	 * @param[in] samplerStateOverrides sampler state override array or empty
	 * @param[in] stateOverrides pipeline state override array or empty
	 */
	ID<GraphicsPipeline> loadGraphicsPipeline(const fs::path& path,
		ID<Framebuffer> framebuffer, bool useAsyncRecording = false,
		bool loadAsync = true, uint8 subpassIndex = 0, uint32 maxBindlessCount = 0,
		const map<string, Pipeline::SpecConstValue>& specConstValues = {},
		const map<string, GraphicsPipeline::SamplerState>& samplerStateOverrides = {},
		const map<uint8, GraphicsPipeline::State>& stateOverrides = {});
	
	/**
	 * @brief Loads compute pipeline from the resource pack shaders.
	 * 
	 * @param[in] path target compute pipeline path
	 * @param useAsyncRecording can be used for multithreaded commands recording
	 * @param loadAsync load pipeline asynchronously without blocking
	 * @param maxBindlessCount maximum bindless descriptor count
	 * @param[in] specConsts specialization constants array or empty
	 * @param[in] samplerStateOverrides sampler state override array or empty
	 */
	ID<ComputePipeline> loadComputePipeline(const fs::path& path,
		bool useAsyncRecording = false, bool loadAsync = true, uint32 maxBindlessCount = 0,
		const map<string, Pipeline::SpecConstValue>& specConstValues = {},
		const map<string, GraphicsPipeline::SamplerState>& samplerStateOverrides = {});

	/* TODO: refactor
	shared_ptr<Model> loadModel(const fs::path& path);
	void loadModelBuffers(shared_ptr<Model> model);

	Ref<Buffer> loadBuffer(shared_ptr<Model> model, ModelData::Accessor accessor,
		Buffer::Bind bind, Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true); // TODO: offset, count?
	Ref<Buffer> loadVertexBuffer(shared_ptr<Model> model, ModelData::Primitive primitive,
		Buffer::Bind bind, const vector<ModelData::Attribute::Type>& attributes,
		Buffer::Access access = Buffer::Access::None,
		Buffer::Strategy strategy = Buffer::Strategy::Default, bool loadAsync = true);
	*/

	/**
	 * @brief Returns current loaded buffer isntance.
	 * @details Useful inside "BufferLoaded" event.
	 */
	ID<Buffer> getLoadedBuffer() const noexcept { return loadedBuffer; }

	/*******************************************************************************************************************
	 * @brief Loads scene from the resource pack.
	 * 
	 * @param[in] path target scene path
	 * @param addRootEntity create root entity for scene
	 */
	void loadScene(const fs::path& path, bool addRootEntity = true);
	/**
	 * @brief Destroys all current scene entities.
	 */
	void clearScene();

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Stores curent scene to the scene directory.
	 * @param[in] path target scene parh
	 */
	void storeScene(const fs::path& path);
	#endif

	#if !GARDEN_DEBUG
	/**
	 * @brief Returns pack reader instance.
	 * @warning Use with caution, background task are using it at runtime.
	 */
	pack::Reader& getPackReader() noexcept { return packReader; }
	#endif

	/**
	 * @brief Returns resource system instance.
	 */
	static ResourceSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
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
#include "garden/hash.hpp"
#include "garden/animate.hpp"
#include "garden/resource/image.hpp"
#include "garden/system/graphics.hpp"
#include <queue>

#if GARDEN_PACK_RESOURCES
#include "pack/reader.hpp"
#endif

namespace garden
{

using namespace math;
using namespace ecsm;
using namespace garden::graphics;

/**
 * @brief Additional image load flags.
 */
enum class ImageLoadFlags : uint8
{
	None = 0x00,       /**< No additional image load flags. */
	LoadSync = 0x01,   /**< Load image synchronously. (Blocking call) */
	LoadShared = 0x02, /**< Load and share instance on second load call. */
	LoadArray = 0x04,  /**< Load as image array. (Slice to layers) */
	ArrayType = 0x08,  /**< Always load with array image type. (Texture2DArray) */
	LinearData = 0x10  /**< Load image data in linear color space. */
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(ImageLoadFlags)

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
		GraphicsPipeline pipeline;
		void* renderPass = nullptr;
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
		fs::path path = "";
		ID<Buffer> instance = {};
	};
	struct ImageQueueItem
	{
		Image image;
		Buffer staging;
		vector<fs::path> paths = {};
		uint2 realSize = uint2(0);
		ID<Image> instance = {};
	};
	struct LoadedBufferItem
	{
		fs::path path = "";
		ID<Buffer> instance = {};
	};
	struct LoadedImageItem
	{
		vector<fs::path> paths = {};
		ID<Image> instance = {};
	};
	
	map<Hash128, Ref<Buffer>> sharedBuffers;
	map<Hash128, Ref<Image>> sharedImages;
	map<Hash128, Ref<DescriptorSet>> sharedDescriptorSets;
	map<Hash128, Ref<Animation>> sharedAnimations;
	queue<GraphicsQueueItem> loadedGraphicsQueue; // TODO: We can use here lock free concurrent queue.
	queue<ComputeQueueItem> loadedComputeQueue;
	queue<BufferQueueItem> loadedBufferQueue;
	queue<ImageQueueItem> loadedImageQueue;
	vector<LoadedBufferItem> loadedBufferArray;
	vector<LoadedImageItem> loadedImageArray;
	mutex queueLocker = {};
	Hash128::State hashState = nullptr;
	ID<Buffer> loadedBuffer = {};
	ID<Image> loadedImage = {};
	vector<fs::path> loadedImagePaths = {};
	fs::path loadedBufferPath = {};

	#if GARDEN_PACK_RESOURCES
	pack::Reader packReader;
	#endif
	#if GARDEN_EDITOR
	fs::path appResourcesPath;
	fs::path appCachesPath;
	Version appVersion;
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
	 * @note Loads from the images directory in debug build.
	 * 
	 * @param[in] path target image resource path
	 * @param[out] data image pixel data container
	 * @param[out] size loaded image size in pixels
	 * @param[out] format loaded image data format
	 * @param threadIndex thread index the pool (-1 = single threaded)
	 */
	void loadImageData(const fs::path& path, vector<uint8>& data,
		uint2& size, Image::Format& format, int32 threadIndex = -1) const;

	/**
	 * @brief Loads cubemap image data (pixels) the resource pack.
	 * @note Loads from the images directory in debug build.
	 * 
	 * @param[in] path target cubemap image resource path
	 * @param[out] left left cubemap image pixel data container
	 * @param[out] right right cubemap image pixel data container
	 * @param[out] bottom bottom cubemap image pixel data container
	 * @param[out] top top cubemap image pixel data container
	 * @param[out] back back cubemap image pixel data container
	 * @param[out] front front cubemap image pixel data container
	 * @param[out] size loaded cubemap image size in pixels
	 * @param[out] format loaded cubemap image data format
	 * @param threadIndex thread index the pool (-1 = single threaded)
	 */
	void loadCubemapData(const fs::path& path, vector<uint8>& left, vector<uint8>& right,
		vector<uint8>& bottom, vector<uint8>& top, vector<uint8>& back, vector<uint8>& front,
		uint2& size, Image::Format& format, int32 threadIndex = -1) const;
	
	/**
	 * @brief Loads image data (pixels) from the memory file. (MT-Safe)
	 * 
	 * @param[in] data image file data 
	 * @param dataSize image file data size in bytes
	 * @param fileType image file data type
	 * @param[out] pixels image pixel data container
	 * @param[out] imageSize loaded image size in pixels
	 * @param[out] format loaded image data format
	 */
	static void loadImageData(const uint8* data, psize dataSize, ImageFileType fileType,
		vector<uint8>& pixels, uint2& imageSize, Image::Format& format);

	/*******************************************************************************************************************
	 * @brief Loads image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] paths target image resource path array
	 * @param bind image bind type (affects driver optimization)
	 * @param maxMipCount maximum mipmap level count (0 = unlimited)
	 * @param strategy image memory allocation strategy
	 * @param flags additinoal image load flags
	 */
	Ref<Image> loadImageArray(const vector<fs::path>& paths, Image::Bind bind, uint8 maxMipCount = 1,
		Image::Strategy strategy = Buffer::Strategy::Default, ImageLoadFlags flags = ImageLoadFlags::None);

	/**
	 * @brief Loads image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] path target image resource path
	 * @param bind image bind type (affects driver optimization)
	 * @param maxMipCount maximum mipmap level count (0 = unlimited)
	 * @param strategy image memory allocation strategy
	 * @param flags additinoal image load flags
	 */
	Ref<Image> loadImage(const fs::path& path, Image::Bind bind, uint8 maxMipCount = 1,
		Image::Strategy strategy = Buffer::Strategy::Default, ImageLoadFlags flags = ImageLoadFlags::None)
	{
		return loadImageArray({ path }, bind, maxMipCount, strategy, flags);
	}

	/**
	 * @brief Destroys shared image if it's the last one.
	 */
	void destroyShared(const Ref<Image>& image);

	/**
	 * @brief Returns current loaded image instance.
	 * @details Useful inside "ImageLoaded" event.
	 */
	ID<Image> getLoadedImage() const noexcept { return loadedImage; }
	/**
	 * @brief Returns current loaded image path array.
	 * @details Useful inside "ImageLoaded" event.
	 */
	const vector<fs::path>& getLoadedImagePaths() const noexcept { return loadedImagePaths; }

	// TODO: storeImage

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
	 * @brief Destroys shared buffer if it's the last one.
	 */
	void destroyShared(const Ref<Buffer>& buffer);

	/**
	 * @brief Returns current loaded buffer isntance.
	 * @details Useful inside "BufferLoaded" event.
	 */
	ID<Buffer> getLoadedBuffer() const noexcept { return loadedBuffer; }

	/*******************************************************************************************************************
	 * @brief Create shared graphics descriptor set instance.
	 * 
	 * @param hash shared descriptor set hash
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	Ref<DescriptorSet> createSharedDescriptorSet(
		const Hash128& hash, ID<GraphicsPipeline> graphicsPipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);

	/**
	 * @brief Create shared graphics descriptor set instance.
	 *
	 * @param hash shared descriptor set hash
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	Ref<DescriptorSet> createSharedDescriptorSet(
		const Hash128& hash, ID<ComputePipeline> computePipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);

	/**
	 * @brief Destroys shared descriptor set if it's the last one.
	 */
	void destroyShared(const Ref<DescriptorSet>& descriptorSet);

	/*******************************************************************************************************************
	 * @brief Loads graphics pipeline from the resource pack shaders.
	 * @note Loads from the shaders directory in debug build.
	 * 
	 * @param[in] path target graphics pipeline resource path
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
	 * @note Loads from the shaders directory in debug build.
	 * 
	 * @param[in] path target compute pipeline resource path
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

	/*******************************************************************************************************************
	 * @brief Loads scene from the resource pack.
	 * @note Loads from the scenes directory in debug build.
	 * 
	 * @param[in] path target scene resource path
	 * @param addRootEntity create root entity for a scene
	 */
	ID<Entity> loadScene(const fs::path& path, bool addRootEntity = false);
	/**
	 * @brief Destroys all current scene entities.
	 */
	void clearScene();

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Stores curent scene to the scenes directory.
	 * 
	 * @param[in] path target scene resource path
	 * @param rootEntity custom scene root or null
	 */
	void storeScene(const fs::path& path, ID<Entity> rootEntity = {});
	#endif

	/*******************************************************************************************************************
	 * @brief Loads animation from the resource pack.
	 * @note Loads from the animations directory in debug build.
	 * 
	 * @param[in] path target animation resource path
	 * @param loadShared load and share instance on second load call
	 */
	Ref<Animation> loadAnimation(const fs::path& path, bool loadShared = false);

	/**
	 * @brief Destroys shared animation if it's the last one.
	 */
	void destroyShared(const Ref<Animation>& animation);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Stores animation to the animations directory.
	 * 
	 * @param[in] path target animation resource path
	 * @param animation target animation instance
	 */
	void storeAnimation(const fs::path& path, ID<Animation> animation);
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
	static ResourceSystem* get() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
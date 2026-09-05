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

/***********************************************************************************************************************
 * @file Application resource loading functions. (images, models, shaders, scenes, fonts, animations, sounds, etc.)
 */

#pragma once
#include "garden/hash.hpp"
#include "garden/font.hpp"
#include "garden/animate.hpp"
#include "garden/system/thread.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/pipeline/ray-tracing.hpp"
#include <queue>

#if GARDEN_PACK_RESOURCES
#include "pack/reader.hpp"
#endif

namespace garden
{

using namespace garden::graphics;

/**
 * @brief Game or application resource loader. (images, models, shader, scenes, sounds, etc.)
 * 
 * @details
 * Manages the process of loading, and also the organization of various game assets or resources such as 
 * images or textures, models, shaders, audio or sound files, scenes and other data that games need to run.
 * 
 * Registers events: ImageLoaded, BufferLoaded.
 */
class ResourceSystem : public System, public Singleton<ResourceSystem>
{
public:
	static const vector<string_view> imageFileExts;      /**< Supported image file extensions. */
	static const vector<Image::FileType> imageFileTypes; /**< Supported image file types. */

	/**
	 * @brief Pipeline load options container.
	 */
	struct PipelineLoadOptions
	{
		Pipeline::SpecConstValues* specConstValues = nullptr;     /**< Specialization constants array or null. */
		Pipeline::SamplerStates* samplerStateOverrides = nullptr; /**< Pipeline sampler state overrides or null. */
		uint32 maxBindlessCount = 0;                 /**< Maximum pipeline bindless descriptor array size. */
		float taskPriority = TaskPriority::pipeline; /**< Thread pool pipeline load task priority. */
		bool loadAsync = true; /**< Load pipeline asynchronously without blocking. [See isReady() func] */
	};
	/**
	 * @brief Graphics pipeline load options container.
	 */
	struct GraphicsLoadOptions final : public PipelineLoadOptions
	{
		GraphicsPipeline::PipelineStates* pipelineStateOverrides = nullptr; /**< Pipeline state overrides or null. */
		GraphicsPipeline::BlendStates* blendStateOverrides = nullptr; /**< Pipeline blend state overrides or null. */
		GraphicsPipeline::ShaderOverrides* shaderOverrides = nullptr; /**< Pipeline shader code overrides or null. */
	};
	/**
	 * @brief Compute pipeline load options container.
	 */
	struct ComputeLoadOptions final : public PipelineLoadOptions
	{
		ComputePipeline::ShaderOverrides* shaderOverrides = nullptr; /**< Pipeline shader code overrides or null. */
	};
	/**
	 * @brief Ray tracing pipeline load options container.
	 */
	struct RayTracingLoadOptions final : public PipelineLoadOptions
	{
		RayTracingPipeline::ShaderOverrides* shaderOverrides = nullptr; /**< Pipeline shader code overrides or null. */
	};
protected:
	//******************************************************************************************************************
	struct GraphicsQueueItem final
	{
		GraphicsPipeline pipeline;
		ID<GraphicsPipeline> instance = {};
	};
	struct ComputeQueueItem final
	{
		ComputePipeline pipeline;
		ID<ComputePipeline> instance = {};
	};
	struct RayTracingQueueItem final
	{
		RayTracingPipeline pipeline;
		ID<RayTracingPipeline> instance = {};
	};
	struct BufferQueueItem final
	{
		Buffer buffer;
		Buffer staging;
		fs::path path = "";
		ID<Buffer> bufferInstance = {};
	};
	struct ImageQueueItem final
	{
		Image image;
		Buffer staging;
		vector<fs::path> paths = {};
		ID<Image> instance = {};
	};

	struct LoadedBufferItem final
	{
		fs::path path = "";
		ID<Buffer> instance = {};
	};
	struct LoadedImageItem final
	{
		vector<fs::path> paths = {};
		ID<Image> instance = {};
	};
	
	tsl::robin_map<Hash128, Ref<Image>> sharedImages;
	tsl::robin_map<Hash128, Ref<Buffer>> sharedBuffers;
	tsl::robin_map<Hash128, Ref<DescriptorSet>> sharedDescriptorSets;
	tsl::robin_map<Hash128, Ref<Animation>> sharedAnimations;
	tsl::robin_map<Hash128, Ref<Font>> sharedFonts;
	queue<GraphicsQueueItem> loadedGraphicsQueue; // TODO: We can use here lock free concurrent queue.
	queue<ComputeQueueItem> loadedComputeQueue;
	queue<RayTracingQueueItem> loadedRayTracingQueue;
	queue<BufferQueueItem> loadedBufferQueue;
	queue<ImageQueueItem> loadedImageQueue;
	vector<LoadedImageItem> loadedImageArray;
	vector<LoadedBufferItem> loadedBufferArray;
	mutex queueLocker = {};
	ID<Image> loadedImage = {};
	ID<Buffer> loadedBuffer = {};
	vector<fs::path> loadedImagePaths = {};
	fs::path loadedBufferPath = "";
	Version appVersion = {};

	#if GARDEN_PACK_RESOURCES
	pack::Reader packReader = {};
	#endif
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	fs::path appResourcesPath = "";
	fs::path appCachePath = "";
	#endif

	/**
	 * @brief Creates a new resource system instance.
	 * @param setSingleton set system singleton instance
	 */
	ResourceSystem(bool setSingleton = true);

	void dequeuePipelines();
	void dequeueImages();
	void dequeueBuffers();

	virtual void init();
	virtual void input();
	virtual void fileChange();
	virtual void fileDrop();
	
	bool loadOrConvertCubemap(const fs::path& path, vector<uint8>& nx, vector<uint8>& px, 
		vector<uint8>& ny, vector<uint8>& py, vector<uint8>& nz, vector<uint8>& pz, 
		uint2& size, Image::Format& format, int32 threadIndex) const noexcept;
	bool loadOrConvertImage(const fs::path& path, vector<uint8>& pixels, uint4& size, 
		Image::Type& type, Image::Format& format, int32 threadIndex) const noexcept;
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Default font path array.
	 */
	vector<fs::path> defaultFontPaths = { "dejavu-sans-mono" };
	/**
	 * @brief Supporting noto font paths.
	 */
	vector<fs::path> notoFontPaths =
	{
		"noto-sans/base", "noto-sans/japanese", "noto-sans/tchinese", "noto-sans/schinese", 
		"noto-sans/korean", "noto-sans/arabic", "noto-sans/devanagari", "noto-sans/hebrew", 
		"noto-sans/thai", "noto-sans/bengali", "noto-sans/urdu"
	};

	/**
	 * @brief Loads image pixels from the resource pack.
	 * @note Loads from the images directory in debug build.
	 * 
	 * @param[in] path target image resource path
	 * @param[out] pixels loaded image pixel data
	 * @param[out] size loaded image size in pixels
	 * @param[out] type loaded image dimensionality type
	 * @param[out] format loaded image data format
	 * @param threadIndex thread index in the pool (-1 = single threaded)
	 *
	 * @return True on success, otherwise false and missing image data.
	 */
	bool loadImageData(const fs::path& path, vector<uint8>& pixels, uint4& size, 
		Image::Type& type, Image::Format& format, int32 threadIndex = -1) const noexcept;
	/**
	 * @brief Loads image pixels from the resource pack.
	 * @note Loads from the images directory in debug build.
	 * 
	 * @param[in] paths target image resource path array
	 * @param pathCount image resource path array size
	 * @param[out] pixelArrays loaded image pixel data arrays
	 * @param[out] size loaded image size in pixels
	 * @param[out] type loaded image dimensionality type
	 * @param[out] format loaded image data format
	 * @param threadIndex thread index in the pool (-1 = single threaded)
	 *
	 * @return True on success, otherwise false and missing image data.
	 */
	bool loadImageData(const fs::path* paths, psize pathCount, vector<vector<uint8>>& pixelArrays, 
		uint4& size, Image::Type& type, Image::Format& format, int32 threadIndex = -1) const noexcept;

	/**
	 * @brief Loads cubemap image pixels the resource pack.
	 * @note Loads from the images directory in debug build.
	 * 
	 * @param[in] path target cubemap image resource path
	 * @param[out] nx negative X side image pixel data container
	 * @param[out] px positive X side image pixel data container
	 * @param[out] ny negative Y side image pixel data container
	 * @param[out] py positive Y side image pixel data container
	 * @param[out] nz negative Z side image pixel data container
	 * @param[out] px positive Z side image pixel data container
	 * @param[out] size loaded cubemap image size in pixels
	 * @param[out] format loaded image data format
	 * @param threadIndex thread index the pool (-1 = single threaded)
	 *
	 * @return True on success, otherwise false and missing image data.
	 */
	bool loadCubemapData(const fs::path& path, vector<uint8>& nx, vector<uint8>& px, 
		vector<uint8>& ny, vector<uint8>& py, vector<uint8>& nz, vector<uint8>& pz, 
		uint2& size, Image::Format& format, int32 threadIndex = -1) const noexcept;

	/*******************************************************************************************************************
	 * @brief Loads image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] paths target image resource path array
	 * @param pathCount image resource path array size
	 * @param loadAsync load image asynchronously without blocking
	 */
	ID<Image> loadImage(const fs::path* paths, psize pathCount, bool loadAsync = true);
	/**
	 * @brief Loads image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] path target image resource path
	 * @param loadAsync load image asynchronously without blocking
	 */
	ID<Image> loadImage(const fs::path& path, bool loadAsync = true) { return loadImage(&path, 1, loadAsync); }

	/**
	 * @brief Loads shared image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] paths target image resource path array
	 * @param pathCount image resource path array size
	 * @param loadAsync load image asynchronously without blocking
	 */
	Ref<Image> loadSharedImage(const fs::path* paths, psize pathCount, bool loadAsync = true);
	/**
	 * @brief Loads shared image from the resource pack.
	 * @note Loads from the images directory in debug build.
	 *
	 * @param[in] path target image resource path
	 * @param loadAsync load image asynchronously without blocking
	 */
	Ref<Image> loadSharedImage(const fs::path& path, bool loadAsync = true)
	{
		return loadSharedImage(&path, 1, loadAsync);
	}
	/**
	 * @brief Destroys shared image if it's the last one.
	 * @param[in] image target shared image reference
	 */
	void destroyShared(Ref<Image>& image);

	/**
	 * @brief Stores specified image to the images directory.
	 * 
	 * @param[in] path target image resource path
	 * @param[in] pixels image pixel data
	 * @param size image size in pixels
	 * @param fileType image file container type
	 * @param imageType image dimensionality type
	 * @param imageFormat required image data format
	 * @param quality image visual quality (0.0 - 1.0)
	 * @param effort image compression effort (0.0 - 1.0)
	 * @param[in] directory scene resource directory
	 */
	void storeImage(const fs::path& path, const void* pixels, uint3 size, 
		Image::FileType fileType, Image::Type imageType, Image::Format imageFormat, 
		float quality = 1.0f, float effort = 0.7f, const fs::path& directory = "");
	/**
	 * @brief Stores specified image to the images directory.
	 * 
	 * @param[in] path target image resource path
	 * @param[in] pixels image pixel data
	 * @param size image size in pixels
	 * @param fileType image file container type
	 * @param imageType image dimensionality type
	 * @param imageFormat required image data format
	 * @param quality image visual quality (0.0 - 1.0)
	 * @param effort image compression effort (0.0 - 1.0)
	 * @param[in] directory scene resource directory
	 */
	void storeImage(const fs::path& path, const vector<uint8>& pixels, uint3 size, 
		Image::FileType fileType, Image::Type imageType, Image::Format imageFormat, 
		float quality = 1.0f, float effort = 0.7f, const fs::path& directory = "")
	{
		GARDEN_ASSERT(pixels.size() == toBinarySize(size.x * size.y * size.z, imageFormat));
		storeImage(path, pixels.data(), size, fileType, imageType, imageFormat, quality, effort, directory);
	}

	/*******************************************************************************************************************
	 * @brief Combines images into the one image.
	 *
	 * @param[in] inputPaths input image path array
	 * @param[in] outputPath output image path
	 * @param fileType output image file container type
	 * @param imageFormat required image data format
	 * @param quality image visual quality (0.0 - 1.0)
	 * @param effort image compression effort (0.0 - 1.0)
	 * @param threadIndex thread index in the pool (-1 = single threaded)
	 */
	void combineImages(const vector<fs::path>& inputPaths, const fs::path& outputPath, Image::FileType fileType, 
		Image::Format imageFormat, float quality = 1.0f, float effort = 1.0f, int32 threadIndex = -1);

	/**
	 * @brief Renormalizes specified normal map image.
	 *
	 * @param[in] path target image resource path
	 * @param fileType output image file container type
	 * @param quality image visual quality (0.0 - 1.0)
	 * @param effort image compression effort (0.0 - 1.0)
	 * @param threadIndex thread index in the pool (-1 = single threaded)
	 */
	void renormalizeImage(const fs::path& path, Image::FileType fileType, 
		float quality = 1.0f, float effort = 0.7f, int32 threadIndex = -1);

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

	/*******************************************************************************************************************
	 * @brief Loads buffer from the resource pack.
	 * @note Loads from the resources directory in debug build.
	 *
	 * @param[in] path target buffer resource path
	 * @param taskPriority thread pool buffer load task priority
	 * @param loadAsync load buffer asynchronously without blocking
	 */
	ID<Buffer> loadBuffer(const fs::path& path, float taskPriority = TaskPriority::normal, bool loadAsync = true);
	/**
	 * @brief Loads shared buffer from the resource pack.
	 * @note Loads from the resources directory in debug build.
	 *
	 * @param[in] path target buffer resource path
	 * @param taskPriority thread pool buffer load task priority
	 * @param loadAsync load buffer asynchronously without blocking
	 */
	Ref<Buffer> loadSharedBuffer(const fs::path& path, float taskPriority = TaskPriority::normal, bool loadAsync = true);
	/**
	 * @brief Destroys shared buffer if it's the last one.
	 * @param[in] buffer target shared buffer reference
	 */
	void destroyShared(Ref<Buffer>& buffer);

	/**
	 * @brief Returns current loaded buffer instance.
	 * @details Useful inside "BufferLoaded" event.
	 */
	ID<Buffer> getLoadedBuffer() const noexcept { return loadedBuffer; }

	/*******************************************************************************************************************
	 * @brief Creates shared graphics descriptor set instance.
	 * 
	 * @param hash shared descriptor set hash
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	Ref<DescriptorSet> createSharedDS(const Hash128& hash, ID<GraphicsPipeline> graphicsPipeline,
		DescriptorSet::Uniforms&& uniforms, uint8 index = 0);

	/**
	 * @brief Creates shared graphics descriptor set instance.
	 *
	 * @param hash shared descriptor set hash
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	Ref<DescriptorSet> createSharedDS(const Hash128& hash, ID<ComputePipeline> computePipeline,
		DescriptorSet::Uniforms&& uniforms, uint8 index = 0);

	/**
	 * @brief Destroys shared descriptor set if it's the last one.
	 * @param[in] descriptorSet target shared descriptor set reference
	 */
	void destroyShared(Ref<DescriptorSet>& descriptorSet);

	/*******************************************************************************************************************
	 * @brief Loads graphics pipeline from the resource pack shaders.
	 * @note Loads from the shaders directory in debug build.
	 * 
	 * @param[in] path target graphics pipeline resource path
	 * @param framebuffer parent pipeline framebuffer
	 * @param[in] options graphics pipeline load options or null
	 * 
	 * @throw GardenError if failed to load graphics pipeline.
	 */
	ID<GraphicsPipeline> loadGraphicsPipeline(const fs::path& path, 
		ID<Framebuffer> framebuffer, const GraphicsLoadOptions* options = nullptr);
	/**
	 * @brief Loads compute pipeline from the resource pack shaders.
	 * @note Loads from the shaders directory in debug build.
	 * 
	 * @param[in] path target compute pipeline resource path
	 * @param[in] options compute pipeline load options or null
	 * 
	 * @throw GardenError if failed to load compute pipeline.
	 */
	ID<ComputePipeline> loadComputePipeline(const fs::path& path, const ComputeLoadOptions* options = nullptr);
	/**
	 * @brief Loads ray tracing pipeline from the resource pack shaders.
	 * @note Loads from the shaders directory in debug build.
	 * 
	 * @param[in] path target ray tracing pipeline resource path
	 * @param[in] options ray tracing pipeline load options or null
	 * 
	 * @throw GardenError if failed to load ray tracing pipeline.
	 */
	ID<RayTracingPipeline> loadRayTracingPipeline(const fs::path& path, const RayTracingLoadOptions* options = nullptr);

	/*******************************************************************************************************************
	 * @brief Loads scene from the resource pack.
	 * @note Loads from the scenes directory in debug build.
	 * 
	 * @param[in] path target scene resource path
	 * @param addRootEntity create root entity for a scene
	 */
	ID<Entity> loadScene(const fs::path& path, bool addRootEntity = false);
	/**
	 * @brief Stores curent scene to the scenes directory.
	 * 
	 * @param[in] path target scene resource path
	 * @param rootEntity custom scene root or null
	 * @param[in] directory scene resource directory
	 */
	void storeScene(const fs::path& path, ID<Entity> rootEntity = {}, const fs::path& directory = "");

	/**
	 * @brief Destroys all current scene entities.
	 */
	void clearScene();

	/*******************************************************************************************************************
	 * @brief Loads animation from the resource pack.
	 * @note Loads from the animations directory in debug build.
	 * @param[in] path target animation resource path
	 */
	ID<Animation> loadAnimation(const fs::path& path);
	/**
	 * @brief Loads shared animation from the resource pack.
	 * @note Loads from the animations directory in debug build.
	 * @param[in] path target animation resource path
	 */
	Ref<Animation> loadSharedAnimation(const fs::path& path);
	/**
	 * @brief Destroys shared animation if it's the last one.
	 * @param[in] animation target shared animation reference
	 */
	void destroyShared(Ref<Animation>& animation);

	/**
	 * @brief Stores animation to the animations directory.
	 * 
	 * @param[in] path target animation resource path
	 * @param animation target animation instance
	 * @param[in] directory animation resource directory
	 */
	void storeAnimation(const fs::path& path, ID<Animation> animation, const fs::path& directory = "");

	/*******************************************************************************************************************
	 * @brief Loads font from the resource pack.
	 * @note Loads from the fonts directory in debug build.
	 *
	 * @param[in] path target font resource path
	 * @param faceIndex font face index to load
	 * @param logMissing log error when font does not exist
	 */
	Ref<Font> loadFont(const fs::path& path, int32 faceIndex = 0, bool logMissing = true);
	/**
	 * @brief Loads fonts from the resource pack.
	 * @note Loads from the fonts directory in debug build.
	 *
	 * @param[in] paths target font resource paths or null
	 * @param faceIndex font face index to load
	 * @param loadNoto also load sans noto supporting fonts
	 */
	FontArray loadFonts(const vector<fs::path>& paths = {}, int32 faceIndex = 0, bool loadNoto = true);

	/**
	 * @brief Destroys shared font if it's the last one.
	 * @param[in] font target shared font reference
	 */
	void destroyShared(Ref<Font>& font);
	/**
	 * @brief Destroys shared fonts if it's the last one.
	 * @param[in] fonts target shared fonts array
	 */
	void destroyShared(FontArray& fonts);

	/*******************************************************************************************************************
	 * @brief Loads file data from the resource pack.
	 * @note Loads from the resources directory in debug build.
	 *
	 * @param[in] path target file resource path
	 * @param[out] data loaded data buffer
	 */
	bool loadData(const fs::path& path, vector<uint8>& data);

	#if GARDEN_PACK_RESOURCES
	/**
	 * @brief Returns pack reader instance.
	 * @warning Use with caution, background task are using it at runtime.
	 */
	pack::Reader& getPackReader() noexcept { return packReader; }
	#endif
};

} // namespace garden
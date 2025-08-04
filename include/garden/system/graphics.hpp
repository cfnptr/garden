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

/***********************************************************************************************************************
 * @file
 * @brief Graphics rendering functions.
 */

#pragma once
#include "garden/system/input.hpp"
#include "garden/graphics/constants.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/pipeline/ray-tracing.hpp"
#include "garden/graphics/acceleration-structure/tlas.hpp" // TODO: move somewhere?

namespace garden
{

using namespace garden::graphics;
class GraphicsSystem;

#if GARDEN_DEBUG || GARDEN_EDITOR
/**
 * @brief Sets GPU resource debug name. (visible in GPU profiler)
 * @param resource target resource instance
 * @param[in] name resource debug name
 */
#define SET_RESOURCE_DEBUG_NAME(resource, name) GraphicsSystem::Instance::get()->setDebugName(resource, name)
#else
/**
 * @brief Sets GPU resource debug name. (visible in GPU profiler)
 * @param resource target resource instance
 * @param[in] name resource debug name
 */
#define SET_RESOURCE_DEBUG_NAME(resource, name) (void)0
#endif

/***********************************************************************************************************************
 * @brief Contains information about swapchain changes.
 */
struct SwapchainChanges final
{
	bool framebufferSize = false; /**< Is framebuffer size has been changed. */
	bool imageCount = false;     /**< Is swapchain image count been changed. */
	bool vsyncState = false;      /**< Is V-Sync state has been changed. */
};

/**
 * @brief Graphics GPU resource and command manager.
 * 
 * @details
 * Graphics system manages the resources of the GPU, which includes allocating and deallocating memory 
 * for images (textures), buffers (vertex, index, uniform...), framebuffers, descriptor sets and 
 * shader pipelines (programs). It also responsible for recording a set of rendering, computation and 
 * transfer commands in command buffers before submitting them to the GPU for execution.
 * 
 * Registers events: Render, Present, SwapchainRecreate.
 */
class GraphicsSystem final : public System, public Singleton<GraphicsSystem>
{
	DescriptorSet::Buffers cameraConstantsBuffers;
	CameraConstants currentCameraConstants = {};
	uint64 frameIndex = 0, tickIndex = 0;
	double beginSleepClock = 0.0;
	ID<Buffer> cubeVertexBuffer = {};
	ID<Buffer> quadVertexBuffer = {};
	ID<ImageView> emptyTexture = {};
	ID<ImageView> whiteTexture = {};
	ID<ImageView> greenTexture = {};
	ID<ImageView> normalMapTexture = {};
	ID<Framebuffer> swapchainFramebuffer = {};
	float renderScale = 0.0f;
	bool asyncRecording = false;
	bool forceRecreateSwapchain = false;
	bool isFramebufferSizeValid = false;
	bool outOfDateSwapchain = false;
	SwapchainChanges swapchainChanges;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	ID<GraphicsPipeline> linePipeline;
	ID<GraphicsPipeline> aabbPipeline;
	#endif

	/**
	 * @brief Creates a new graphics system instance.
	 * 
	 * @param windowSize target OS window size (in units)
	 * @param isFullscreen create a fullscreen window
	 * @param isDecorated decorate window with top bar and borders
	 * @param useVsync use vertical synchronization (V-Sync)
	 * @param useTripleBuffering use swapchain triple buffering
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param setSingleton set system singleton instance
	 */
	GraphicsSystem(uint2 windowSize = InputSystem::defaultWindowSize, 
		bool isFullscreen = !GARDEN_DEBUG & !GARDEN_OS_LINUX, bool isDecorated = true,
		bool useVsync = true, bool useTripleBuffering = true, bool useAsyncRecording = true, 
		bool setSingleton = true);
	/**
	 * @brief Destroys graphics system instance.
	 */
	~GraphicsSystem() final;

	void preInit();
	void preDeinit();
	void input();
	void update();
	void present();

	friend class ecsm::Manager;
public:
	ID<Entity> camera = {};           /**< Current main render camera. */
	ID<Entity> directionalLight = {}; /**< Current main directional light. (Sun) */
	uint16 maxFPS = 60;               /**< Frames per second limit. */
	bool useVsync = false;            /**< Vertical synchronization state. (V-Sync) */
	bool useTripleBuffering = false;  /**< Swapchain triple buffering state. */

	/**
	 * @brief Returns frame render scale.
	 * @details Useful for scaling forward/deferred framebuffer.
	 */
	float getRenderScale();
	/**
	 * @brief Sets frame render scale.
	 * @note It signals swapchain size change.
	 */
	void setRenderScale(float renderScale);

	/**
	 * @brief Sets global illumination buffer world space position.
	 * @details See the @ref getCameraConstants().
	 * @param giBufferPos target GI buffer position
	 */
	void setGiBufferPos(float3 giBufferPos, float intensity = 1.0f) noexcept
	{
		currentCameraConstants.giBufferPos = (f32x4)float4(giBufferPos, 0.0f);
	}
	/**
	 * @brief Sets shadow color and intensity
	 * @details See the @ref getCameraConstants().
	 *
	 * @param shadowColor target shadow color value
	 * @param intensity shadow intensity value
	 */
	void setShadowColor(float3 shadowColor, float intensity = 1.0f) noexcept
	{
		currentCameraConstants.shadowColor = (f32x4)float4(shadowColor, intensity);
	}
	/**
	 * @brief Sets sky color and intensity. (Pre multiplied with 1/Pi!)
	 * @details See the @ref getCameraConstants().
	 * @param skyColor target sky color value
	 */
	void setSkyColor(float3 skyColor) noexcept
	{
		currentCameraConstants.skyColor = (f32x4)float4(skyColor, 0.0f);
	}
	/**
	 * @brief Sets emissive coefficient. (Produces maximum brightness)
	 * @details See the @ref getCameraConstants().
	 * @param emissiveCoeff target emissive coefficient
	 */
	void setEmissiveCoeff(float emissiveCoeff) noexcept { currentCameraConstants.emissiveCoeff = emissiveCoeff; }

	/**
	 * @brief Returns swapchain framebuffer size.
	 * @note It may differ from the input framebuffer size.
	 */
	uint2 getFramebufferSize() const noexcept;
	/**
	 * @brief Returns scaled by render scale framebuffer size.
	 * @details Useful for scaling forward/deferred framebuffer.
	 */
	uint2 getScaledFramebufferSize() const noexcept;

	/**
	 * @brief Returns current render pass framebuffer.
	 * @details Set by the @ref Framebuffer::beginRenderPass().
	 */
	ID<Framebuffer> getCurrentFramebuffer() const noexcept;
	/**
	 * @brief Returns current render subpass index.
	 * @details Changes by the @ref Framebuffer::nextSubpass().
	 */
	uint8 getCurrentSubpassIndex() const noexcept;
	/**
	 * @brief Is current render pass use multithreaded commands recording.
	 * @details Changes by the @ref Framebuffer::nextSubpass().
	 */
	bool isCurrentRenderPassAsync() const noexcept;

	/*******************************************************************************************************************
	 * @brief Returns current frame index since the application launch.
	 * @details It does not count frames when the window is minimized.
	 */
	uint64 getCurrentFrameIndex() const noexcept { return frameIndex; }
	/**
	 * @brief Returns current tick index since the application launch.
	 * @details Each tick is a update function call by the manager.
	 */
	uint64 getCurrentTickIndex() const noexcept { return tickIndex; }

	/**
	 * @brief Can a frame be rendered on the current tick.
	 * @details In some cases we can't render to the window. (ex. it may be hidden) 
	 */
	bool canRender() const noexcept { return isFramebufferSizeValid; }
	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }

	/**
	 * @brief Returns true if target GPU has ray tracing support.
	 * @note Without hardware support we can't use ray tracing pipelines.
	 */
	bool hasRayTracing() const noexcept;
	/**
	 * @brief Returns true if target GPU has ray query support.
	 * @note Without hardware support we can't use ray query in shaders.
	 */
	bool hasRayQuery() const noexcept;

	/**
	 * @brief Returns current swapchain changes.
	 * @details Use it on "SwapchainRecreate" event.
	 */
	const SwapchainChanges& getSwapchainChanges() const noexcept { return swapchainChanges; }

	/**
	 * @brief Returns total in-flight frame count.
	 * @note Used for creating required uniform buffer count.
	 */
	uint32 getInFlightCount() const noexcept;
	/**
	 * @brief Returns current in-flight frame index.
	 * @details It changes after each framebuffer present on the screen.
	 */
	uint32 getInFlightIndex() const noexcept;

	/**
	 * @brief Returns current swapchain asynchronous thread count.
	 * @details Useful for an async rendering commands.
	 */
	uint32 getThreadCount() const noexcept;

	/**
	 * @brief Is current swapchain out of date.
	 * @details Swapchain will be recreated on next frame with valid framebuffer size.
	 */
	bool isOutOfDateSwapchain() const noexcept { return outOfDateSwapchain; }

	/*******************************************************************************************************************
	 * @brief Returns cube vertex buffer.
	 * @details Allocates if it is not created yet.
	 */
	ID<Buffer> getCubeVertexBuffer();
	/**
	 * @brief Returns quad vertex buffer.
	 * @details Allocates if it is not created yet.
	 */
	ID<Buffer> getQuadVertexBuffer();

	/**
	 * @brief Returns empty texture image view. (0, 0, 0, 0)
	 * @details Allocates if it is not created yet.
	 */
	ID<ImageView> getEmptyTexture();
	/**
	 * @brief Returns white texture image view. (255, 255, 255, 255)
	 * @details Allocates if it is not created yet.
	 */
	ID<ImageView> getWhiteTexture();
	/**
	 * @brief Returns white texture image view. (0, 255, 0, 255)
	 * @details Allocates if it is not created yet.
	 */
	ID<ImageView> getGreenTexture();
	/**
	 * @brief Returns normal map texture image view. (127, 127, 255, 255)
	 * @details Allocates if it is not created yet.
	 */
	ID<ImageView> getNormalMapTexture();

	/**
	 * @brief Returns current swapchain framebuffer.
	 * @warning Swapchain framebuffer image can be reallocated on swapchain resize.
	 */
	ID<Framebuffer> getSwapchainFramebuffer() const noexcept { return swapchainFramebuffer; }
	/**
	 * @brief Returns current render camera constants buffer.
	 * @details Use it to access common camera properties inside shader. 
	 */
	const DescriptorSet::Buffers& getCameraConstantsBuffers() const noexcept { return cameraConstantsBuffers; }
	/**
	 * @brief Returns current render camera constants.
	 * @details Useful for transformation matrices.
	 */
	const CameraConstants& getCameraConstants() const noexcept { return currentCameraConstants; }

	/**
	 * @brief Manually signal swapchain changes.
	 * @param[in] changes target swapchain changes.
	 */
	void recreateSwapchain(const SwapchainChanges& changes);

	/*******************************************************************************************************************
	 * @brief Creates a new buffer instance.
	 * 
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param[in] data target buffer data
	 * @param size buffer size in bytes
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	ID<Buffer> createBuffer(Buffer::Usage usage, Buffer::CpuAccess cpuAccess, const void* data, uint64 size,
		Buffer::Location location = Buffer::Location::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default);
	/**
	 * @brief Creates a new empty buffer instance. (Undefined initial data)
	 * 
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param size buffer size in bytes
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	ID<Buffer> createBuffer(Buffer::Usage usage, Buffer::CpuAccess cpuAccess, uint64 size,
		Buffer::Location location = Buffer::Location::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		return createBuffer(usage, cpuAccess, nullptr, size, location, strategy);
	}
	/**
	 * @brief Creates a new buffer instance.
	 * 
	 * @tparam T data array element type
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param[in] data target buffer data
	 * @param count data array element count
	 * @param offset data array element offset
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	template<typename T = float>
	ID<Buffer> createBuffer(
		Buffer::Usage usage, Buffer::CpuAccess cpuAccess, const vector<T>& data, psize count = 0, psize offset = 0,
		Buffer::Location location = Buffer::Location::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(usage, cpuAccess, data.data() + offset,
				(data.size() - offset) * sizeof(T), location, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(usage, cpuAccess, data.data() + offset, count * sizeof(T), location, strategy);
		}
	}
	/**
	 * @brief Creates a new buffer instance.
	 * 
	 * @tparam T data array element type
	 * @param usage buffer usage flags
	 * @param cpuAccess buffer CPU side access
	 * @param[in] data target buffer data
	 * @param count data array element count
	 * @param offset data array element offset
	 * @param location buffer preferred location
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	template<typename T = float, psize S>
	ID<Buffer> createBuffer(
		Buffer::Usage usage, Buffer::CpuAccess cpuAccess, const array<T, S>& data, psize count = 0, psize offset = 0,
		Buffer::Location location = Buffer::Location::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(usage, cpuAccess, data.data() + offset,
				(data.size() - offset) * sizeof(T), location, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(usage, cpuAccess, data.data() + offset, count * sizeof(T), location, strategy);
		}
	}

	/**
	 * @brief Destroys buffer instance.
	 * @param buffer target buffer instance or null
	 */
	void destroy(ID<Buffer> buffer);
	/**
	 * @brief Destroys shared buffer instance.
	 * @param[in] buffer target buffer reference or null
	 */
	void destroy(const Ref<Buffer>& buffer)
	{
		if (buffer.isLastRef())
			destroy(ID<Buffer>(buffer));
	}
	/**
	 * @brief Destroys vector with buffer instances.
	 * @param[in] buffers target vector with buffer instances or/and nulls
	 */
	void destroy(const vector<ID<Buffer>>& buffers)
	{
		for (auto buffer : buffers)
			destroy(buffer);
	}
	/**
	 * @brief Destroys descriptor set buffer instances for each swapchain.
	 * @param[in] dsBuffer target descriptor set buffer instances or/and nulls
	 */
	void destroy(const DescriptorSet::Buffers& dsBuffer)
	{
		for (const auto& buffers : dsBuffer)
		{
			for (auto buffer : buffers)
				destroy(buffer);
		}
	}

	/**
	 * @brief Returns buffer data accessor.
	 * @param buffer target buffer instance
	 */
	View<Buffer> get(ID<Buffer> buffer) const;
	/**
	 * @brief Returns buffer data accessor.
	 * @param buffer target buffer instance
	 */
	View<Buffer> get(const Ref<Buffer>& buffer) const { return get(ID<Buffer>(buffer)); }

	/*******************************************************************************************************************
	 * @brief Creates a new image (texture) instance.
	 * 
	 * @param type image dimensionality
	 * @param format image data format
	 * @param usage image usage flags
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Type type, Image::Format format, Image::Usage usage, const Image::Mips& data, uint3 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined);
	/**
	 * @brief Creates a new 3D image (texture) instance.
	 * 
	 * @param format image data format
	 * @param usage image usage flags
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Usage usage, const Image::Mips& data, uint3 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		return createImage(Image::Type::Texture3D, format, usage, data, size, strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 2D image (texture) instance.
	 * @details Automatically detects if image has array type.
	 * 
	 * @param format image data format
	 * @param usage image usage flags
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Usage usage, const Image::Mips& data, uint2 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture2DArray : Image::Type::Texture2D;
		return createImage(imageType, format, usage, data, uint3(size.x, size.y, 1), strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 1D image (texture) instance.
	 * @details Automatically detects if image has array type.
	 * 
	 * @param format image data format
	 * @param usage image usage flags
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Usage usage, const Image::Mips& data, uint32 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture1DArray : Image::Type::Texture1D;
		return createImage(imageType, format, usage, data, uint3(size, 1, 1), strategy, dataFormat);
	}
	/**
	 * @brief Creates a new cubemap image (texture) instance.
	 * 
	 * @param format image data format
	 * @param usage image usage flags
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createCubemap(
		Image::Format format, Image::Usage usage, const Image::Mips& data, uint2 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		return createImage(Image::Type::Cubemap, format, usage, data, uint3(size.x, size.y, 1), strategy, dataFormat);
	}

	/* 
	 * TODO: create 2 images with the same shared memory allocation.
	 * https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
	 * ID<Image> createSharedImage(;
	 */
	
	/**
	 * @brief Destroys image instance.
	 * @param image target image instance or null
	 */
	void destroy(ID<Image> image);
	/**
	 * @brief Destroys shared image instance.
	 * @param[in] image target image reference or null
	 */
	void destroy(const Ref<Image>& image)
	{
		if (image.isLastRef())
			destroy(ID<Image>(image));
	}
	/**
	 * @brief Destroys vector with image instances.
	 * @param[in] images target vector with image instances or/and nulls
	 */
	void destroy(const vector<ID<Image>>& images)
	{
		for (auto image : images)
			destroy(image);
	}

	/**
	 * @brief Returns image data accessor.
	 * @param image target image instance
	 */
	View<Image> get(ID<Image> image) const;
	/**
	 * @brief Returns image data accessor.
	 * @param image target image instance
	 */
	View<Image> get(const Ref<Image>& image) const { return get(ID<Image>(image)); }

	/*******************************************************************************************************************
	 * @brief Creates a new image (texture) view instance.
	 * 
	 * @param image target image instance
	 * @param type image view dimensionality
	 * @param format image view data format (undefined = image format)
	 * @param baseMip image view base mip index
	 * @param mipCount image view mip count (0 = image mip count)
	 * @param baseLayer image view base layer index
	 * @param layerCount image view layer count (0 = image layer count)
	 */
	ID<ImageView> createImageView(ID<Image> image, Image::Type type, Image::Format format = Image::Format::Undefined,
		uint8 baseMip = 0, uint8 mipCount = 0, uint32 baseLayer = 0, uint32 layerCount = 0);

	/**
	 * @brief Destroys image view instance.
	 * @param imageView target image view instance or null
	 */
	void destroy(ID<ImageView> imageView);
	/**
	 * @brief Destroys shared image view instance.
	 * @param[in] imageView target image view reference or null
	 */
	void destroy(const Ref<ImageView>& imageView)
	{
		if (imageView.isLastRef())
			destroy(ID<ImageView>(imageView));
	}
	/**
	 * @brief Destroys vector with image view instances.
	 * @param[in] imageViews target vector with image view instances or/and nulls
	 */
	void destroy(const vector<ID<ImageView>>& imageViews)
	{
		for (auto imageView : imageViews)
			destroy(imageView);
	}

	/**
	 * @brief Returns image view data accessor.
	 * @param imageView target image view instance
	 */
	View<ImageView> get(ID<ImageView> imageView) const;
	/**
	 * @brief Returns image view data accessor.
	 * @param imageView target image view instance
	 */
	View<ImageView> get(const Ref<ImageView>& imageView) const { return get(ID<ImageView>(imageView)); }

	/*******************************************************************************************************************
	 * @brief Creates a new framebuffer instance.
	 * 
	 * @param size framebuffer size in pixels
	 * @param colorAttachments color attachments or empty array
	 * @param colorAttachments depth or/and stencil attachment or null
	 */
	ID<Framebuffer> createFramebuffer(uint2 size, vector<Framebuffer::OutputAttachment>&& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment = {});
	/**
	 * @brief Creates a new framebuffer instance.
	 * 
	 * @param size framebuffer size in pixels
	 * @param[in] subpasses target framebuffer subpasses
	 */
	ID<Framebuffer> createFramebuffer(uint2 size, vector<Framebuffer::Subpass>&& subpasses);

	/**
	 * @brief Destroys framebuffer instance.
	 * @param framebuffer target framebuffer instance or null
	 */
	void destroy(ID<Framebuffer> framebuffer);
	/**
	 * @brief Destroys shared framebuffer instance.
	 * @param[in] framebuffer target framebuffer reference or null
	 */
	void destroy(const Ref<Framebuffer>& framebuffer)
	{
		if (framebuffer.isLastRef())
			destroy(ID<Framebuffer>(framebuffer));
	}
	/**
	 * @brief Destroys vector with framebuffer instances.
	 * @param[in] framebuffers target vector with framebuffer instances or/and nulls
	 */
	void destroy(const vector<ID<Framebuffer>>& framebuffers)
	{
		for (auto framebuffer : framebuffers)
			destroy(framebuffer);
	}

	/**
	 * @brief Returns framebuffer data accessor.
	 * @param framebuffer target framebuffer instance
	 */
	View<Framebuffer> get(ID<Framebuffer> framebuffer) const;
	/**
	 * @brief Returns framebuffer data accessor.
	 * @param framebuffer target framebuffer instance
	 */
	View<Framebuffer> get(const Ref<Framebuffer>& framebuffer) const { return get(ID<Framebuffer>(framebuffer)); }

	/*******************************************************************************************************************
	 * @brief Creates a new sampler instance.
	 * @param[in] state target sampler state
	 */
	ID<Sampler> createSampler(const Sampler::State& state);
	 /**
	 * @brief Destroys sampler instance.
	 * @param sampler target sampler instance or null
	 */
	void destroy(ID<Sampler> sampler);
	/**
	 * @brief Destroys shared sampler instance.
	 * @param[in] sampler target sampler reference or null
	 */
	void destroy(const Ref<Sampler>& sampler)
	{
		if (sampler.isLastRef())
			destroy(ID<Sampler>(sampler));
	}
	/**
	 * @brief Destroys vector with sampler instances.
	 * @param[in] samplers target vector with sampler instances or/and nulls
	 */
	void destroy(const vector<ID<Sampler>>& samplers)
	{
		for (auto sampler : samplers)
			destroy(sampler);
	}

	/**
	 * @brief Returns sampler data accessor.
	 * @param sampler target sampler instance
	 */
	View<Sampler> get(ID<Sampler> sampler) const;
	 /**
	  * @brief Returns sampler data accessor.
	  * @param sampler target sampler instance
	  */
	View<Sampler> get(const Ref<Sampler>& sampler) const { return get(ID<Sampler>(sampler)); }

	/*******************************************************************************************************************
	 * @brief Destroys graphics pipeline instance.
	 * @param graphicsPipeline target graphics pipeline instance or null
	 */
	void destroy(ID<GraphicsPipeline> graphicsPipeline);
	/**
	 * @brief Destroys shared graphics pipeline instance.
	 * @param[in] graphicsPipeline target graphics pipeline reference or null
	 */
	void destroy(const Ref<GraphicsPipeline>& graphicsPipeline)
	{
		if (graphicsPipeline.isLastRef())
			destroy(ID<GraphicsPipeline>(graphicsPipeline));
	}
	/**
	 * @brief Destroys vector with graphics pipeline instances.
	 * @param[in] graphicsPipelines target vector with graphics pipeline instances or/and nulls
	 */
	void destroy(const vector<ID<GraphicsPipeline>>& graphicsPipelines)
	{
		for (auto graphicsPipeline : graphicsPipelines)
			destroy(graphicsPipeline);
	}

	/**
	 * @brief Returns graphics pipeline data accessor.
	 * @param graphicsPipeline target graphics pipeline instance
	 */
	View<GraphicsPipeline> get(ID<GraphicsPipeline> graphicsPipeline) const;
	/**
	 * @brief Returns graphics pipeline data accessor.
	 * @param graphicsPipeline target graphics pipeline instance
	 */
	View<GraphicsPipeline> get(const Ref<GraphicsPipeline>& graphicsPipeline) const
	{
		return get(ID<GraphicsPipeline>(graphicsPipeline));
	}

	/**
	 * @brief Destroys compute pipeline instance.
	 * @param computePipeline target compute pipeline instance or null
	 */
	void destroy(ID<ComputePipeline> computePipeline);
	/**
	 * @brief Destroys shared compute pipeline instance.
	 * @param[in] computePipeline target compute pipeline reference or null
	 */
	void destroy(const Ref<ComputePipeline>& computePipeline)
	{
		if (computePipeline.isLastRef())
			destroy(ID<ComputePipeline>(computePipeline));
	}
	/**
	 * @brief Destroys vector with compute pipeline instances.
	 * @param[in] computePipelines target vector with compute pipeline instances or/and nulls
	 */
	void destroy(const vector<ID<ComputePipeline>>& computePipelines)
	{
		for (auto computePipeline : computePipelines)
			destroy(computePipeline);
	}

	/**
	 * @brief Returns compute pipeline data accessor.
	 * @param computePipeline target compute pipeline instance
	 */
	View<ComputePipeline> get(ID<ComputePipeline> computePipeline) const;
	/**
	 * @brief Returns compute pipeline data accessor.
	 * @param computePipeline target compute pipeline instance
	 */
	View<ComputePipeline> get(const Ref<ComputePipeline>& computePipeline) const
	{
		return get(ID<ComputePipeline>(computePipeline));
	}

	/**
	 * @brief Destroys ray tracing pipeline instance.
	 * @param rayTracingPipeline target ray tracing pipeline instance or null
	 */
	void destroy(ID<RayTracingPipeline> rayTracingPipeline);
	/**
	 * @brief Destroys shared ray tracing pipeline instance.
	 * @param[in] rayTracingPipeline target ray tracing pipeline reference or null
	 */
	void destroy(const Ref<RayTracingPipeline>& rayTracingPipeline)
	{
		if (rayTracingPipeline.isLastRef())
			destroy(ID<RayTracingPipeline>(rayTracingPipeline));
	}
	/**
	 * @brief Destroys vector with ray tracing pipeline instances.
	 * @param[in] rayTracingPipelines target vector with ray tracing pipeline instances or/and nulls
	 */
	void destroy(const vector<ID<RayTracingPipeline>>& rayTracingPipelines)
	{
		for (auto rayTracingPipeline : rayTracingPipelines)
			destroy(rayTracingPipeline);
	}

	/**
	 * @brief Returns ray tracing pipeline data accessor.
	 * @param rayTracingPipeline target tracing pipeline pipeline instance
	 */
	View<RayTracingPipeline> get(ID<RayTracingPipeline> rayTracingPipeline) const;
	/**
	 * @brief Returns ray tracing pipeline data accessor.
	 * @param rayTracingPipeline target ray tracing pipeline instance
	 */
	View<RayTracingPipeline> get(const Ref<RayTracingPipeline>& rayTracingPipeline) const
	{
		return get(ID<RayTracingPipeline>(rayTracingPipeline));
	}

	/*******************************************************************************************************************
	 * @brief Create a new graphics descriptor set instance.
	 * 
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param[in] samplers dynamic sampler array (mutable uniforms)
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
		DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers = {}, uint8 index = 0);
	/**
	 * @brief Create a new compute descriptor set instance.
	 * 
	 * @param computePipeline target compute pipeline
	 * @param[in] uniforms shader uniform array
	 * @param[in] samplers dynamic sampler array (mutable uniforms)
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<ComputePipeline> computePipeline,
		DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers = {}, uint8 index = 0);
	/**
	 * @brief Create a new ray tracing descriptor set instance.
	 * 
	 * @param rayTracingPipeline target ray tracing pipeline
	 * @param[in] uniforms shader uniform array
	 * @param[in] samplers dynamic sampler array (mutable uniforms)
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<RayTracingPipeline> rayTracingPipeline,
		DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers = {}, uint8 index = 0);

	/**
	 * @brief Destroys descriptor set instance.
	 * @param descriptorSet target descriptor set instance or null
	 */
	void destroy(ID<DescriptorSet> descriptorSet);
	/**
	 * @brief Destroys shared descriptor set instance.
	 * @param[in] descriptorSet target descriptor set reference or null
	 */
	void destroy(const Ref<DescriptorSet>& descriptorSet)
	{
		if (descriptorSet.isLastRef())
			destroy(ID<DescriptorSet>(descriptorSet));
	}
	/**
	 * @brief Destroys vector with descriptor set instances.
	 * @param[in] descriptorSets target vector with descriptor set instances or/and nulls
	 */
	void destroy(const vector<ID<DescriptorSet>>& descriptorSets)
	{
		for (auto descriptorSet : descriptorSets)
			destroy(descriptorSet);
	}

	/**
	 * @brief Returns descriptor set data accessor.
	 * @param descriptorSet target descriptor set instance
	 */
	View<DescriptorSet> get(ID<DescriptorSet> descriptorSet) const;
	/**
	 * @brief Returns descriptor set data accessor.
	 * @param descriptorSet target descriptor set instance
	 */
	View<DescriptorSet> get(const Ref<DescriptorSet>& descriptorSet) const
	{
		return get(ID<DescriptorSet>(descriptorSet));
	}

	/*******************************************************************************************************************
	 * @brief Create a new graphics bottom level acceleration structure instance. (BLAS)
	 * 
	 * @param[in] geometryArray target triangle geometry array
	 * @param geometryCount geometry array size
	 * @param flags acceleration structure build flags
	 */
	ID<Blas> createBlas(const Blas::TrianglesBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags = {});
	/**
	 * @brief Create a new graphics bottom level acceleration structure instance. (BLAS)
	 * 
	 * @param[in] geometryArray target AABB geometry array
	 * @param geometryCount geometry array size
	 * @param flags acceleration structure build flags
	 */
	ID<Blas> createBlas(const Blas::AabbsBuffer* geometryArray, uint32 geometryCount, BuildFlagsAS flags = {});

	/**
	 * @brief Destroys bottom level acceleration structure instance. (BLAS)
	 * @param blas target BLAS instance or null
	 */
	void destroy(ID<Blas> blas);
	/**
	 * @brief Destroys shared bottom level acceleration structure instance.
	 * @param[in] blas target BLAS reference or null
	 */
	void destroy(const Ref<Blas>& blas)
	{
		if (blas.isLastRef())
			destroy(ID<Blas>(blas));
	}
	/**
	 * @brief Destroys vector with BLAS instances.
	 * @param[in] samplers target vector with BLAS instances or/and nulls
	 */
	void destroy(const vector<ID<Blas>>& blases)
	{
		for (auto blas : blases)
			destroy(blas);
	}

	/**
	 * @brief Returns bottom level acceleration structure data accessor. (BLAS)
	 * @param blas target BLAS instance
	 */
	View<Blas> get(ID<Blas> blas) const;
	/**
	 * @brief Returns bottom level acceleration structure data accessor. (BLAS)
	 * @param blas target BLAS instance
	 */
	View<Blas> get(const Ref<Blas>& blas) const { return get(ID<Blas>(blas)); }

	/*******************************************************************************************************************
	 * @brief Create a new graphics top level acceleration structure instance. (TLAS)
	 * 
	 * @param instances TLAS instance array
	 * @param instanceBuffer target TLAS instance buffer
	 * @param flags acceleration structure build flags
	 */
	ID<Tlas> createTlas(vector<Tlas::InstanceData>&& instances, ID<Buffer> instanceBuffer, BuildFlagsAS flags = {});

	/**
	 * @brief Destroys top level acceleration structure instance. (TLAS)
	 * @param tlas target TLAS instance or null
	 */
	void destroy(ID<Tlas> tlas);
	/**
	 * @brief Destroys shared top level acceleration structure instance.
	 * @param[in] tlas target TLAS reference or null
	 */
	void destroy(const Ref<Tlas>& tlas)
	{
		if (tlas.isLastRef())
			destroy(ID<Tlas>(tlas));
	}
	/**
	 * @brief Destroys vector with TLAS instances.
	 * @param[in] samplers target vector with TLAS instances or/and nulls
	 */
	void destroy(const vector<ID<Tlas>>& tlases)
	{
		for (auto tlas : tlases)
			destroy(tlas);
	}

	/**
	 * @brief Returns top level acceleration structure data accessor. (TLAS)
	 * @param tlas target TLAS instance
	 */
	View<Tlas> get(ID<Tlas> tlas) const;
	/**
	 * @brief Returns top level acceleration structure data accessor. (TLAS)
	 * @param tlas target TLAS instance
	 */
	View<Tlas> get(const Ref<Tlas>& tlas) const { return get(ID<Tlas>(tlas)); }
	
	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Is current command buffer in the recording state.
	 * @note You should finish commands recording before ending this frame.
	 */
	bool isRecording() const noexcept;
	/**
	 * @brief Starts command buffer commands recording. (Supports multithreading)
	 * @param commandBufferType target command buffer type
	 */
	void startRecording(CommandBufferType commandBufferType);
	/**
	 * @brief Stops command buffer commands recording.
	 * @note You can still append more rendering command to the stopped command buffer.
	 */
	void stopRecording();
	/**
	 * @brief Returns true if target command is busy right now.
	 * @warning This is expensive operation, call only couple of times per frame!
	 * @param commandBufferType target command buffer type
	 */
	bool isBusy(CommandBufferType commandBufferType);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Draws line wireframe. (Debug Only)
	 * 
	 * @param[in] mvp line model view projection matrix
	 * @param startPoint line beginning point
	 * @param endPoint line ending point
	 * @param color line wireframe color
	 */
	void drawLine(const f32x4x4& mvp, f32x4 startPoint, f32x4 endPoint, f32x4 color = f32x4::one);
	/**
	 * @brief Draws axis aligned bounding box wireframe. (Debug Only)
	 * 
	 * @param[in] mvp box model view projection matrix
	 * @param color box wireframe color
	 */
	void drawAabb(const f32x4x4& mvp, f32x4 color = f32x4::one);

	/*******************************************************************************************************************
	 * @brief Sets buffer debug name. (visible in GPU profiler)
	 * @param buffer target buffer instance
	 * @param[in] name buffer debug name
	 */
	void setDebugName(ID<Buffer> buffer, const string& name);
	/**
	 * @brief Sets buffer debug name. (visible in GPU profiler)
	 * @param buffer target buffer instance
	 * @param[in] name buffer debug name
	 */
	void setDebugName(const Ref<Buffer>& buffer, const string& name) { setDebugName(ID<Buffer>(buffer), name); }

	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param image target image instance
	 * @param[in] name image debug name
	 */
	void setDebugName(ID<Image> image, const string& name);
	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param image target image instance
	 * @param[in] name image debug name
	 */
	void setDebugName(const Ref<Image>& image, const string& name) { setDebugName(ID<Image>(image), name); }

	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param imageView target image view instance
	 * @param[in] name image view debug name
	 */
	void setDebugName(ID<ImageView> imageView, const string& name);
	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param imageView target image view instance
	 * @param[in] name image view debug name
	 */
	void setDebugName(const Ref<ImageView>& imageView, const string& name)
	{
		setDebugName(ID<ImageView>(imageView), name);
	}

	/**
	 * @brief Sets framebuffer debug name. (visible in GPU profiler)
	 * @param framebuffer target framebuffer instance
	 * @param[in] name framebuffer debug name
	 */
	void setDebugName(ID<Framebuffer> framebuffer, const string& name);
	/**
	 * @brief Sets framebuffer debug name. (visible in GPU profiler)
	 * @param instance target framebuffer instance
	 * @param[in] name framebuffer debug name
	 */
	void setDebugName(const Ref<Framebuffer>& framebuffer, const string& name)
	{
		setDebugName(ID<Framebuffer>(framebuffer), name);
	}

	/**
	 * @brief Sets sampler debug name. (visible in GPU profiler)
	 * @param sampler target sampler instance
	 * @param[in] name sampler debug name
	 */
	void setDebugName(ID<Sampler> sampler, const string& name);
	 /**
	  * @brief Sets sampler debug name. (visible in GPU profiler)
	  * @param instance target sampler instance
	  * @param[in] name sampler debug name
	  */
	void setDebugName(const Ref<Sampler>& sampler, const string& name) { setDebugName(ID<Sampler>(sampler), name); }

	/**
	 * @brief Sets descriptor set debug name. (visible in GPU profiler)
	 * @param descriptorSet target descriptor set instance
	 * @param[in] name descriptor set debug name
	 */
	void setDebugName(ID<DescriptorSet> descriptorSet, const string& name);
	/**
	 * @brief Sets descriptor set debug name. (visible in GPU profiler)
	 * @param descriptorSet target descriptor set instance
	 * @param[in] name descriptor set debug name
	 */
	void setDebugName(const Ref<DescriptorSet>& descriptorSet, const string& name)
	{
		setDebugName(ID<DescriptorSet>(descriptorSet), name);
	}

	/**
	 * @brief Sets BLAS set debug name. (visible in GPU profiler)
	 * @param blas target BLAS instance
	 * @param[in] name BLAS debug name
	 */
	void setDebugName(ID<Blas> blas, const string& name);
	/**
	 * @brief Sets BLAS debug name. (visible in GPU profiler)
	 * @param blas target BLAS instance
	 * @param[in] name BLAS debug name
	 */
	void setDebugName(const Ref<Blas>& blas, const string& name) { setDebugName(ID<Blas>(blas), name); }

	/**
	 * @brief Sets TLAS set debug name. (visible in GPU profiler)
	 * @param tlas target TLAS instance
	 * @param[in] name TLAS debug name
	 */
	void setDebugName(ID<Tlas> tlas, const string& name);
	/**
	 * @brief Sets TLAS debug name. (visible in GPU profiler)
	 * @param tlas target TLAS instance
	 * @param[in] name TLAS debug name
	 */
	void setDebugName(const Ref<Tlas>& tlas, const string& name) { setDebugName(ID<Tlas>(tlas), name); }
	#endif
};

} // namespace garden
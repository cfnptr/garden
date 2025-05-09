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
	bool bufferCount = false;     /**< Is swapchain buffer count been changed. */
	bool vsyncState = false;      /**< Is V-Sync state has been changed. */
};

/*
 * @brief Descriptor set buffer instances for each swapchain buffer.
 */
using DescriptorSetBuffers = vector<vector<ID<Buffer>>>;

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
	DescriptorSetBuffers cameraConstantsBuffers;
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
	float renderScale = 1.0f;
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
	float getRenderScale() const noexcept { return renderScale; }
	/**
	 * @brief Sets frame render scale.
	 * @note It signals swapchain size change.
	 */
	void setRenderScale(float renderScale);

	/**
	 * @brief Sets shadow color and intensity
	 * @details See the @ref getCurrentCameraConstants().
	 * @param shadowColor target shadow color and intensity
	 */
	void setShadowColor(f32x4 shadowColor) noexcept { currentCameraConstants.shadowColor = shadowColor; }
	/**
	 * @brief Sets emissive coefficient. (Produces maximum brightness)
	 * @details See the @ref getCurrentCameraConstants().
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
	uint64 getFrameIndex() const noexcept { return frameIndex; }
	/**
	 * @brief Returns current tick index since the application launch.
	 * @details Each tick is a update function call by the manager.
	 */
	uint64 getTickIndex() const noexcept { return tickIndex; }

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
	 * @brief Returns current swapchain changes.
	 * @details Use it on "SwapchainRecreate" event.
	 */
	const SwapchainChanges& getSwapchainChanges() const noexcept { return swapchainChanges; }

	/**
	 * @brief Returns current swapchain buffer count.
	 * @details Triple buffering option affects buffer count.
	 */
	uint32 getSwapchainSize() const noexcept;
	/**
	 * @brief Returns current swapchain buffer index.
	 * @details It changes after each framebuffer present on the screen.
	 */
	uint32 getSwapchainIndex() const noexcept;

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
	const DescriptorSetBuffers& getCameraConstantsBuffers() const noexcept { return cameraConstantsBuffers; }

	/**
	 * @brief Manually signal swapchain changes.
	 * @param[in] changes target swapchain changes.
	 */
	void recreateSwapchain(const SwapchainChanges& changes);

	#if GARDEN_DEBUG || GARDEN_EDITOR
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
	void setDebugName(const Ref<Sampler>& sampler, const string& name)
	{
		setDebugName(ID<Sampler>(sampler), name);
	}

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
	#endif

	/*******************************************************************************************************************
	 * @brief Creates a new buffer instance.
	 * 
	 * @param bind buffer bind type
	 * @param access buffer access type
	 * @param[in] data target buffer data
	 * @param size buffer size in bytes
	 * @param usage buffer preferred usage
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Access access, const void* data, uint64 size,
		Buffer::Usage usage = Buffer::Usage::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default);
	/**
	 * @brief Creates a new empty buffer instance. (Undefined initial data)
	 * 
	 * @param bind buffer bind type
	 * @param access buffer access type
	 * @param size buffer size in bytes
	 * @param usage buffer preferred usage
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Access access, uint64 size,
		Buffer::Usage usage = Buffer::Usage::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		return createBuffer(bind, access, nullptr, size, usage, strategy);
	}
	/**
	 * @brief Creates a new buffer instance.
	 * 
	 * @tparam T data array element type
	 * @param bind buffer bind type
	 * @param access buffer access type
	 * @param[in] data target buffer data
	 * @param count data array element count
	 * @param offset data array element offset
	 * @param usage buffer preferred usage
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	template<typename T = float>
	ID<Buffer> createBuffer(
		Buffer::Bind bind, Buffer::Access access, const vector<T>& data, psize count = 0, psize offset = 0,
		Buffer::Usage usage = Buffer::Usage::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(bind, access, data.data() + offset,
				(data.size() - offset) * sizeof(T), usage, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, access, data.data() + offset, count * sizeof(T), usage, strategy);
		}
	}
	/**
	 * @brief Creates a new buffer instance.
	 * 
	 * @tparam T data array element type
	 * @param bind buffer bind type
	 * @param access buffer access type
	 * @param[in] data target buffer data
	 * @param count data array element count
	 * @param offset data array element offset
	 * @param usage buffer preferred usage
	 * @param strategy buffer allocation strategy
	 * 
	 * @throw GardenError if failed to allocate buffer.
	 */
	template<typename T = float, psize S>
	ID<Buffer> createBuffer(
		Buffer::Bind bind, Buffer::Access access, const array<T, S>& data, psize count = 0, psize offset = 0,
		Buffer::Usage usage = Buffer::Usage::Auto, Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(bind, access, data.data() + offset,
				(data.size() - offset) * sizeof(T), usage, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, access, data.data() + offset, count * sizeof(T), usage, strategy);
		}
	}

	/**
	 * @brief Destroys buffer instance.
	 * @param buffer target buffer instance or null
	 */
	void destroy(ID<Buffer> buffer);
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
	void destroy(const DescriptorSetBuffers& dsBuffer)
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
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Type type, Image::Format format, Image::Bind bind, const Image::Mips& data, u32x4 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined);
	/**
	 * @brief Creates a new 3D image (texture) instance.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, u32x4 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		return createImage(Image::Type::Texture3D, format, bind, data, size, strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 2D image (texture) instance.
	 * @details Automatically detects if image has array type.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, uint2 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture2DArray : Image::Type::Texture2D;
		return createImage(imageType, format, bind, data, u32x4(size.x, size.y, 1), strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 1D image (texture) instance.
	 * @details Automatically detects if image has array type.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, int32 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture1DArray : Image::Type::Texture1D;
		return createImage(imageType, format, bind, data, u32x4(size, 1, 1), strategy, dataFormat);
	}
	/**
	 * @brief Creates a new cubemap image (texture) instance.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 * 
	 * @throw GardenError if failed to allocate image.
	 */
	ID<Image> createCubemap(
		Image::Format format, Image::Bind bind, const Image::Mips& data, uint2 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		return createImage(Image::Type::Cubemap, format, bind, data, u32x4(size.x, size.y, 1), strategy, dataFormat);
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
	 * @param graphicsPipeline target compute pipeline
	 * @param[in] uniforms shader uniform array
	 * @param[in] samplers dynamic sampler array (mutable uniforms)
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<ComputePipeline> computePipeline,
		DescriptorSet::Uniforms&& uniforms, DescriptorSet::Samplers&& samplers = {}, uint8 index = 0);

	/**
	 * @brief Destroys descriptor set instance.
	 * @param descriptorSet target descriptor set instance or null
	 */
	void destroy(ID<DescriptorSet> descriptorSet);
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
	void startRecording(CommandBufferType commandBufferType = CommandBufferType::Frame);
	/**
	 * @brief Stops command buffer commands recording.
	 * @note You can still append more rendering command to the stopped command buffer.
	 */
	void stopRecording();

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
	#endif

	//******************************************************************************************************************
	// Returns current render call data.
	//******************************************************************************************************************

	/**
	 * @brief Returns current render camera constants.
	 * @details Useful for transformation matrices.
	 */
	const CameraConstants& getCurrentCameraConstants() const noexcept { return currentCameraConstants; }
};

} // namespace garden
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

using namespace ecsm;
using namespace garden::graphics;
class GraphicsSystem;

/***********************************************************************************************************************
 * @brief Contains information about swapchain changes.
 */
struct SwapchainChanges final
{
	bool framebufferSize = false; /**< Is framebuffer size has been changed. */
	bool bufferCount = false;     /**< Is swapchain buffer count been changed. */
	bool vsyncState = false;      /**< Is V-Sync state has been changed. */
};

/**
 * @brief Responsible for graphics rendering and GPU resource managing.
 * 
 * @details
 * The system manages the resources of the GPU, which includes allocating and deallocating memory for 
 * images (textures), buffers (vertex, index, uniform...), framebuffers, descriptor sets and 
 * shader pipelines (programs). It also responsible for recording a set of rendering, computation and 
 * transfer commands in command buffers before submitting them to the GPU for execution.
 * 
 * Registers events: Render, Present, SwapchainRecreate.
 */
class GraphicsSystem final : public System
{
public:
	using ConstantsBuffer = vector<vector<ID<Buffer>>>;
private:
	ConstantsBuffer cameraConstantsBuffers;
	int2 framebufferSize = int2(0), windowSize = int2(0);
	uint64 frameIndex = 0, tickIndex = 0;
	ID<Framebuffer> swapchainFramebuffer = {};
	ID<Buffer> fullCubeVertices = {};
	ID<ImageView> emptyTexture = {};
	ID<ImageView> whiteTexture = {};
	ID<ImageView> greenTexture = {};
	ID<ImageView> normalMapTexture = {};
	CameraConstants currentCameraConstants = {};
	double beginSleepClock = 0.0;
	bool useThreading = false;
	bool forceRecreateSwapchain = false;
	bool isFramebufferSizeValid = false;
	SwapchainChanges swapchainChanges;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	ID<GraphicsPipeline> aabbPipeline;
	#endif

	static GraphicsSystem* instance;

	/**
	 * @brief Creates a new graphics system instance.
	 * 
	 * @param[in,out] manager manager instance
	 * @param windowSize target OS window size (in units)
	 * @param isFullscreen create a fullscreen window
	 * @param useVsync use vertical synchronization (V-Sync)
	 * @param useTripleBuffering use swapchain triple buffering
	 * @param useThreading use multithreaded command recording
	 */
	GraphicsSystem(Manager* manager,
		int2 windowSize = int2(defaultWindowWidth, defaultWindowHeight),
		bool isFullscreen = !GARDEN_DEBUG, bool useVsync = true,
		bool useTripleBuffering = true, bool useThreading = true);
	/**
	 * @brief Destroys graphics system instance.
	 */
	~GraphicsSystem() final;

	#if GARDEN_EDITOR
	void initializeImGui();
	void terminateImGui();
	void recreateImGui();
	#endif

	void preInit();
	void preDeinit();
	void input();
	void update();
	void present();

	friend class ecsm::Manager;

	//******************************************************************************************************************
public:
	/**
	 * @brief Current main render camera.
	 */
	ID<Entity> camera = {};
	/**
	 * @brief Current main directional light. (sun)
	 */
	ID<Entity> directionalLight = {};
	/**
	 * @brief Target frame rate. (FPS)
	 */
	uint16 frameRate = 60;
	/**
	 * @brief Vertical synchronization state. (V-Sync)
	 */
	bool useVsync = false;
	/**
	 * @brief Swapchain triple buffering state.
	 */
	bool useTripleBuffering = false;

	/**
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
	 * @brief Returns current framebuffer size in pixels.
	 * @details It can change when window or swachain is resized.
	 */
	int2 getFramebufferSize() const noexcept { return framebufferSize; }
	/**
	 * @brief Returns current window size in units.
	 * @note It can differ from the framebuffer size! (e.g on macOS)
	 */
	int2 getWindowSize() const noexcept { return windowSize; }

	/**
	 * @brief Returns true if frame can be rendered on current tick.
	 * @details In some cases we can't render to the window. (ex. it may be hidden) 
	 */
	bool canRender() const noexcept { return isFramebufferSizeValid; }
	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool isUseThreading() const noexcept { return useThreading; }

	/**
	 * @brief Returns current swapchain changes.
	 * @details Use it on "SwapchainRecreate" event.
	 */
	const SwapchainChanges& getSwapchainChanges() const noexcept { swapchainChanges; }

	/**
	 * @brief Does GPU support dynamic rendering feature.
	 * @details See the VK_KHR_dynamic_rendering.
	 */
	bool hasDynamicRendering() const noexcept;
	/**
	 * @brief Does GPU support descriptor indexing feature.
	 * @details See the VK_EXT_descriptor_indexing.
	 */
	bool hasDescriptorIndexing() const noexcept;

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
	 * @brief Returns full cube vertex buffer.
	 * @details Allocates if it is not created yet.
	 */
	ID<Buffer> getFullCubeVertices();
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
	 * @brief Returns white texture image view. (127, 127, 255, 255)
	 * @details Allocates if it is not created yet.
	 */
	ID<ImageView> getNormalMapTexture();

	/**
	 * @brief Sets window title. (UTF-8)
	 * @param title target title string
	 */
	void setWindowTitle(const string& title);

	/**
	 * @brief Returns current swapchain framebuffer.
	 * @warning Swapchain framebuffer image can be reallocated on swapchain resize.
	 */
	ID<Framebuffer> getSwapchainFramebuffer() const noexcept { return swapchainFramebuffer; }
	/**
	 * @brief Returns current render camera constants buffer.
	 * @details Use it to access common camera properties inside shader. 
	 */
	const ConstantsBuffer& getCameraConstantsBuffers() const noexcept { return cameraConstantsBuffers; }

	/**
	 * @brief Manually signal swapchain changes.
	 * @param[in] changes target swapchain changes.
	 */
	void recreateSwapchain(const SwapchainChanges& changes);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/*******************************************************************************************************************
	 * @brief Sets buffer debug name. (visible in GPU profiler)
	 * @param instance target buffer
	 * @param[in] name debug name
	 */
	void setDebugName(ID<Buffer> instance, const string& name);
	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param instance target image
	 * @param[in] name debug name
	 */
	void setDebugName(ID<Image> instance, const string& name);
	/**
	 * @brief Sets image debug name. (visible in GPU profiler)
	 * @param instance target image
	 * @param[in] name debug name
	 */
	void setDebugName(ID<ImageView> instance, const string& name);
	/**
	 * @brief Sets framebuffer debug name. (visible in GPU profiler)
	 * @param instance target framebuffer
	 * @param[in] name debug name
	 */
	void setDebugName(ID<Framebuffer> instance, const string& name);
	/**
	 * @brief Sets descriptor set debug name. (visible in GPU profiler)
	 * @param instance target descriptor set
	 * @param[in] name debug name
	 */
	void setDebugName(ID<DescriptorSet> instance, const string& name);
	/**
	 * @brief Sets GPU resource debug name. (visible in GPU profiler)
	 * @param resource target resource
	 * @param[in] name debug name
	 */
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name) graphicsSystem->setDebugName(resource, name)
	#else
	/**
	 * @brief Sets GPU resource debug name. (visible in GPU profiler)
	 * @param resource target resource
	 * @param name debug name
	 */
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name)
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
	 * @param instance target buffer instance or null
	 */
	void destroy(ID<Buffer> instance);
	/**
	 * @brief Returns buffer data accessor.
	 * @param instance target buffer instance
	 */
	View<Buffer> get(ID<Buffer> instance) const;

	/*******************************************************************************************************************
	 * @brief Creates a new image (texture) instance.
	 * 
	 * @param type image dimensionality
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param[in] size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 */
	ID<Image> createImage(
		Image::Type type, Image::Format format, Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined);

	/**
	 * @brief Creates a new 3D image (texture) instance.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param[in] size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		return createImage(Image::Type::Texture3D, format, bind, data, size, strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 2D image (texture) instance.
	 * @details Automatically detects if image is has array type.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, int2 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture2DArray : Image::Type::Texture2D;
		return createImage(imageType, format, bind, data, int3(size, 1), strategy, dataFormat);
	}
	/**
	 * @brief Creates a new 1D image (texture) instance.
	 * @details Automatically detects if image is has array type.
	 * 
	 * @param format image data format
	 * @param bind image bind type
	 * @param[in] data image data (mips, layers)
	 * @param size image size in pixels
	 * @param strategy image allocation strategy
	 * @param dataFormat data array format
	 */
	ID<Image> createImage(
		Image::Format format, Image::Bind bind, const Image::Mips& data, int32 size,
		Image::Strategy strategy = Image::Strategy::Default, Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ? Image::Type::Texture1DArray : Image::Type::Texture1D;
		return createImage(imageType, format, bind, data, int3(size, 1, 1), strategy, dataFormat);
	}

	/* 
	 * TODO: create 2 images with the same shared memory allocation.
	 * https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
	 * ID<Image> createSharedImage(;
	 */
	
	/**
	 * @brief Destroys image instance.
	 * @param instance target image instance or null
	 */
	void destroy(ID<Image> instance);
	/**
	 * @brief Returns image data accessor.
	 * @param instance target image instance
	 */
	View<Image> get(ID<Image> instance) const;

	/*******************************************************************************************************************
	 * @brief Creates a new image (texture) view instance.
	 * 
	 * @param image target image instance
	 * @param type image view dimensionality
	 * @param format image view data format
	 * @param baseMip image view base mip index
	 * @param mipCount image view mip count
	 * @param baseLayer image view base layer index
	 * @param layerCount image view layer count
	 */
	ID<ImageView> createImageView(ID<Image> image, Image::Type type, Image::Format format = Image::Format::Undefined,
		uint8 baseMip = 0, uint8 mipCount = 1, uint32 baseLayer = 0, uint32 layerCount = 1);

	/**
	 * @brief Destroys image view instance.
	 * @param instance target image view instance or null
	 */
	void destroy(ID<ImageView> instance);
	/**
	 * @brief Returns image view data accessor.
	 * @param instance target image view instance
	 */
	View<ImageView> get(ID<ImageView> instance) const;

	/*******************************************************************************************************************
	 * @brief Creates a new framebuffer instance.
	 * 
	 * @param size framebuffer size in pixels
	 * @param colorAttachments color attachments or empty array
	 * @param colorAttachments depth or/and stencil attachment or null
	 */
	ID<Framebuffer> createFramebuffer(int2 size, vector<Framebuffer::OutputAttachment>&& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment = {});
	
	/**
	 * @brief Creates a new framebuffer instance.
	 * 
	 * @param size framebuffer size in pixels
	 * @param[in] subpasses target framebuffer subpasses
	 */
	ID<Framebuffer> createFramebuffer(int2 size, vector<Framebuffer::Subpass>&& subpasses);

	/**
	 * @brief Destroys framebuffer instance.
	 * @param instance target framebuffer instance or null
	 */
	void destroy(ID<Framebuffer> instance);
	/**
	 * @brief Returns framebuffer data accessor.
	 * @param instance target framebuffer instance
	 */
	View<Framebuffer> get(ID<Framebuffer> instance) const;

	/*******************************************************************************************************************
	 * @brief Destroys graphics pipeline instance.
	 * @param instance target graphics pipeline instance or null
	 */
	void destroy(ID<GraphicsPipeline> instance);
	/**
	 * @brief Returns graphics pipeline data accessor.
	 * @param instance target graphics pipeline instance
	 */
	View<GraphicsPipeline> get(ID<GraphicsPipeline> instance) const;

	/**
	 * @brief Destroys compute pipeline instance.
	 * @param instance target compute pipeline instance or null
	 */
	void destroy(ID<ComputePipeline> instance);
	/**
	 * @brief Returns compute pipeline data accessor.
	 * @param instance target compute pipeline instance
	 */
	View<ComputePipeline> get(ID<ComputePipeline> instance) const;

	/*******************************************************************************************************************
	 * @brief Create a new graphics descriptor set instance.
	 * 
	 * @param graphicsPipeline target graphics pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);
	/**
	 * @brief Create a new compute descriptor set instance.
	 * 
	 * @param graphicsPipeline target compute pipeline
	 * @param[in] uniforms shader uniform array
	 * @param index index of descriptor set in the shader
	 */
	ID<DescriptorSet> createDescriptorSet(ID<ComputePipeline> computePipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);

	/**
	 * @brief Destroys descriptor set instance.
	 * @param instance target descriptor set instance or null
	 */
	void destroy(ID<DescriptorSet> instance);
	/**
	 * @brief Returns descriptor set data accessor.
	 * @param instance target descriptor set instance
	 */
	View<DescriptorSet> get(ID<DescriptorSet> instance) const;

	/**
	 * @brief Returns graphics system instance.
	 * @warning Do not use it if you have several graphics system instances.
	 */
	static GraphicsSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // Graphics system is not created.
		return instance;
	}
	
	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Returns true if current command buffer is in the recording state.
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
	 * @brief Draws axis aligned bounding box wireframe. (Debug Only)
	 * @param[in] mvp box model view projection matrix
	 * @param[in] color box wireframe color
	 */
	void drawAabb(const float4x4& mvp, const float4& color = float4(1.0f));
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
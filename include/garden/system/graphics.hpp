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
 */
struct SwapchainChanges final
{
	bool framebufferSize = false;
	bool bufferCount = false;
	bool vsyncState = false;
};

/**
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

	GraphicsSystem(Manager* manager,
		int2 windowSize = int2(defaultWindowWidth, defaultWindowHeight),
		bool isFullscreen = !GARDEN_DEBUG, bool useVsync = true,
		bool useTripleBuffering = true, bool useThreading = true);
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
	ID<Entity> camera = {};
	ID<Entity> directionalLight = {};
	uint16 frameRate = 60;
	bool useVsync = false, useTripleBuffering = false;
	uint64 getFrameIndex() const noexcept { return frameIndex; }
	uint64 getTickIndex() const noexcept { return tickIndex; }
	int2 getFramebufferSize() const noexcept { return framebufferSize; }
	int2 getWindowSize() const noexcept { return windowSize; }
	bool isUseThreading() const noexcept { return useThreading; }
	const SwapchainChanges& getSwapchainChanges() const noexcept { swapchainChanges; }

	bool hasDynamicRendering() const noexcept;
	bool hasDescriptorIndexing() const noexcept;
	uint32 getSwapchainSize() const noexcept;
	uint32 getSwapchainIndex() const noexcept;

	ID<Buffer> getFullCubeVertices();
	ID<ImageView> getEmptyTexture();
	ID<ImageView> getWhiteTexture();
	ID<ImageView> getGreenTexture();
	ID<ImageView> getNormalMapTexture();
	// Note: be careful when using these functions inside ->get image view code!

	void setWindowTitle(const string& title);

	ID<Framebuffer> getSwapchainFramebuffer() const noexcept { return swapchainFramebuffer; }
	const ConstantsBuffer& getCameraConstantsBuffers() const noexcept { return cameraConstantsBuffers; }
	void recreateSwapchain(const SwapchainChanges& changes);



	#if GARDEN_DEBUG || GARDEN_EDITOR
	//******************************************************************************************************************
	void setDebugName(ID<Buffer> instance, const string& name);
	void setDebugName(ID<Image> instance, const string& name);
	void setDebugName(ID<ImageView> instance, const string& name);
	void setDebugName(ID<Framebuffer> instance, const string& name);
	void setDebugName(ID<DescriptorSet> instance, const string& name);
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name) graphicsSystem->setDebugName(resource, name)
	#else
	#define SET_RESOURCE_DEBUG_NAME(graphicsSystem, resource, name)
	#endif

	//******************************************************************************************************************
	// Returns current render call data.
	//******************************************************************************************************************

	const CameraConstants& getCurrentCameraConstants() const noexcept { return currentCameraConstants; }

	//******************************************************************************************************************
	ID<Buffer> createBuffer(Buffer::Bind bind,
		Buffer::Access access, const void* data, uint64 size,
		Buffer::Usage usage = Buffer::Usage::Auto,
		Buffer::Strategy strategy = Buffer::Strategy::Default);
	
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Access access,
		uint64 size, Buffer::Usage usage = Buffer::Usage::Auto,
		Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		return createBuffer(bind, access, nullptr, size, usage, strategy);
	}
	template<typename T = float>
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Access access,
		const vector<T>& data, psize count = 0, psize offset = 0,
		Buffer::Usage usage = Buffer::Usage::Auto,
		Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(bind, access, data.data() + offset,
				(data.size() - offset) * sizeof(T), usage, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, access, data.data() + offset,
				count * sizeof(T), usage, strategy);
		}
	}
	template<typename T = float, psize S>
	ID<Buffer> createBuffer(Buffer::Bind bind, Buffer::Access access,
		const array<T, S>& data, psize count = 0, psize offset = 0,
		Buffer::Usage usage = Buffer::Usage::Auto,
		Buffer::Strategy strategy = Buffer::Strategy::Default)
	{
		if (count == 0)
		{
			return createBuffer(bind, access, data.data() + offset,
				(data.size() - offset) * sizeof(T), usage, strategy);
		}
		else
		{
			GARDEN_ASSERT(count + offset <= data.size());
			return createBuffer(bind, access, data.data() + offset,
				count * sizeof(T), usage, strategy);
		}
	}

	void destroy(ID<Buffer> instance);
	View<Buffer> get(ID<Buffer> instance) const;

	//******************************************************************************************************************
	ID<Image> createImage(Image::Type type, Image::Format format,
		Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Strategy strategy = Image::Strategy::Default,
		Image::Format dataFormat = Image::Format::Undefined);
	
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, const int3& size,
		Image::Strategy strategy = Image::Strategy::Default,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		return createImage(Image::Type::Texture3D, format,
			bind, data, size, strategy, dataFormat);
	}
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, int2 size,
		Image::Strategy strategy = Image::Strategy::Default,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ?
			Image::Type::Texture2DArray : Image::Type::Texture2D;
		return createImage(imageType, format, bind, data,
			int3(size, 1), strategy, dataFormat);
	}
	ID<Image> createImage(Image::Format format,
		Image::Bind bind, const Image::Mips& data, int32 size,
		Image::Strategy strategy = Image::Strategy::Default,
		Image::Format dataFormat = Image::Format::Undefined)
	{
		GARDEN_ASSERT(!data.empty());
		auto imageType = data[0].size() > 1 ?
			Image::Type::Texture1DArray : Image::Type::Texture1D;
		return createImage(imageType, format, bind, data,
			int3(size, 1, 1), strategy, dataFormat);
	}

	/* TODO: create 2 images with the same shared memory allocation.
	// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html
	ID<Image> createSharedImage(Image::Type type0, Image::Format format0,
		Image::Bind bind0, const Image::Mips& data0, const int3& size0,
		Image::Format dataFormat0, Image::Type type1, Image::Format format1,
		Image::Bind bind1, const Image::Mips& data1, const int3& size1,
		Image::Format dataFormat);
	*/
	
	void destroy(ID<Image> instance);
	View<Image> get(ID<Image> instance) const;

	//******************************************************************************************************************
	ID<ImageView> createImageView(ID<Image> image, Image::Type type,
		Image::Format format = Image::Format::Undefined, uint8 baseMip = 0,
		uint8 mipCount = 1, uint32 baseLayer = 0, uint32 layerCount = 1);
	void destroy(ID<ImageView> instance);
	View<ImageView> get(ID<ImageView> instance) const;

	//******************************************************************************************************************
	ID<Framebuffer> createFramebuffer(int2 size,
		vector<Framebuffer::OutputAttachment>&& colorAttachments,
		Framebuffer::OutputAttachment depthStencilAttachment = {});
	ID<Framebuffer> createFramebuffer(int2 size, vector<Framebuffer::Subpass>&& subpasses);
	void destroy(ID<Framebuffer> instance);
	View<Framebuffer> get(ID<Framebuffer> instance) const;

	//******************************************************************************************************************
	void destroy(ID<GraphicsPipeline> instance);
	View<GraphicsPipeline> get(ID<GraphicsPipeline> instance) const;

	void destroy(ID<ComputePipeline> instance);
	View<ComputePipeline> get(ID<ComputePipeline> instance) const;

	//******************************************************************************************************************
	ID<DescriptorSet> createDescriptorSet(ID<GraphicsPipeline> graphicsPipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);
	ID<DescriptorSet> createDescriptorSet(ID<ComputePipeline> computePipeline,
		map<string, DescriptorSet::Uniform>&& uniforms, uint8 index = 0);
	void destroy(ID<DescriptorSet> instance);
	View<DescriptorSet> get(ID<DescriptorSet> instance) const;
	
	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	bool isRecording() const noexcept;
	void startRecording(CommandBufferType commandBufferType = CommandBufferType::Frame);
	void stopRecording();

	#if GARDEN_DEBUG || GARDEN_EDITOR
	void drawAabb(const float4x4& mvp, const float4& color = float4(1.0f));
	#endif
};

} // namespace garden
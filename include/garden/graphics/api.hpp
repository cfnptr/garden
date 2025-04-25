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

/*******************************************************************************************************************
 * @file
 * @brief Common graphics API functions.
 */

#pragma once
#include "garden/graphics/swapchain.hpp"
#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

namespace garden
{

using namespace garden::graphics;

/**
 * @brief Graphics API backend types.
 */
enum class GraphicsBackend : uint8
{
	VulkanAPI, Count
};

/**
 * @brief Base graphics API class.
 * 
 * @details
 * Graphics API (Application Programming Interface) is a software interface that enables applications to 
 * communicate with and utilize the graphical hardware of a computer system to perform rendering and 
 * compute tasks. These APIs provide a set of functions and protocols for managing graphics rendering, 
 * including drawing 2D and 3D objects, manipulating images and textures, handling shaders 
 * (programs that run on the GPU), and controlling how scenes are rendered to the screen.
 * 
 * Graphics APIs abstract the complexity of interacting directly with the graphics hardware, 
 * allowing developers to write applications that can produce graphical output without needing 
 * to code for specific hardware devices. This abstraction layer enables applications 
 * to run across a wide range of hardware with minimal changes to the application code.
 * 
 * @warning Use graphics API directly with caution!
 */
class GraphicsAPI
{
public:
	/**
	 * @brief Minimal DLSS output size
	 */
	static constexpr uint8 minFramebufferSize = 32;

	/**
	 * @brief Destroyable graphics GPU resource types.
	 */
	enum class DestroyResourceType : uint32
	{
		DescriptorSet, Pipeline, DescriptorPool, DescriptorSetLayout,
		Sampler, Framebuffer, ImageView, Image, Buffer, Count
	};
	/**
	 * @brief Graphics resource destroy data container.
	 */
	struct DestroyResource final
	{
		void* data0 = nullptr;
		void* data1 = nullptr;
		DestroyResourceType type = {};
		uint32 count = 0;
	};
protected:
	vector<DestroyResource> destroyBuffers[frameLag + 1];
	GraphicsBackend backendType = {};
	uint8 fillDestroyIndex = 0;
	uint8 flushDestroyIndex = 1;
	uint8 _alignment0 = 0;

	inline static GraphicsAPI* apiInstance = nullptr;

	GraphicsAPI(const string& appName, uint2 windowSize, bool isFullscreen);
public:
	virtual ~GraphicsAPI();

	int32 threadCount = 0;
	void* window = nullptr;
	Swapchain* swapchain = nullptr;
	LinearPool<Buffer> bufferPool;
	LinearPool<Image> imagePool;
	LinearPool<ImageView> imageViewPool;
	LinearPool<Framebuffer> framebufferPool;
	LinearPool<Sampler> samplerPool;
	LinearPool<GraphicsPipeline> graphicsPipelinePool;
	LinearPool<ComputePipeline> computePipelinePool;
	LinearPool<DescriptorSet> descriptorSetPool;
	map<void*, uint64> renderPasses;
	uint64 graphicsPipelineVersion = 1;
	uint64 computePipelineVersion = 1;
	uint64 bufferVersion = 1;
	uint64 imageVersion = 1;
	CommandBuffer* frameCommandBuffer;
	CommandBuffer* graphicsCommandBuffer;
	CommandBuffer* transferCommandBuffer;
	CommandBuffer* computeCommandBuffer;
	CommandBuffer* currentCommandBuffer = nullptr;
	ID<Framebuffer> currentFramebuffer = {};
	uint32 currentSubpassIndex = 0;
	vector<ID<Pipeline>> currentPipelines;
	vector<PipelineType> currentPipelineTypes;
	vector<ID<Buffer>> currentVertexBuffers;
	vector<ID<Buffer>> currentIndexBuffers;
	bool isCurrentRenderPassAsync = false;
	bool isDeviceIntegrated = false;
	bool forceResourceDestroy = false;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	bool recordGpuTime = false;
	#endif

	/***************************************************************************************************************
	 * @brief Returns graphics API backend type. 
	 */
	GraphicsBackend getBackendType() const noexcept { return backendType; }

	/**
	 * @brief Returns pipeline pool instance from it pointer.
	 *
	 * @param type target pipeline type
	 * @param[in] pipeline pointer to the pipeline
	 */
	ID<Pipeline> getPipeline(PipelineType type, const Pipeline* pipeline) const noexcept
	{
		if (type == PipelineType::Graphics)
			return ID<Pipeline>(graphicsPipelinePool.getID((const GraphicsPipeline*)pipeline));
		else if (type == PipelineType::Compute)
			return ID<Pipeline>(computePipelinePool.getID((const ComputePipeline*)pipeline));
		abort();
	}
	/**
	 * @brief Returns pipeline pool view from it ID.
	 *
	 * @param type target pipeline type
	 * @param pipeline target pipeline instance
	 */
	View<Pipeline> getPipelineView(PipelineType type, ID<Pipeline> pipeline) const noexcept
	{
		if (type == PipelineType::Graphics)
			return View<Pipeline>(graphicsPipelinePool.get(ID<GraphicsPipeline>(pipeline)));
		else if (type == PipelineType::Compute)
			return View<Pipeline>(computePipelinePool.get(ID<ComputePipeline>(pipeline)));
		abort();
	}

	/**
	 * @brief Returns image memory barrier state.
	 * 
	 * @param image target image instace
	 * @param mip image mip level
	 * @param layer image layer index
	 */
	Image::BarrierState& getImageState(ID<Image> image, uint8 mip, uint32 layer) noexcept
	{
		auto imageView = imagePool.get(image);
		auto& barrierStates = ImageExt::getBarrierStates(**imageView);
		return barrierStates[imageView->getLayerCount() * mip + layer];
	}
	 /**
	  * @brief Returns buffer memory barrier state.
	  * @param buffer target buffer insance
	  */
	Buffer::BarrierState& getBufferState(ID<Buffer> buffer) noexcept
	{
		return BufferExt::getBarrierState(**bufferPool.get(buffer));
	}

	/**
	 * @brief Calculate rendering operation auto thread count
	 * @param threadIndex current thread index
	 */
	int32 calcAutoThreadCount(int32& threadIndex) const noexcept
	{
		if (threadIndex < 0)
		{
			threadIndex = 0;
			return threadCount;
		}
		return threadIndex + 1;
	}

	/***************************************************************************************************************
	 * @brief Adds graphics resource data to the destroy buffer.
	 * 
	 * @param type target GPU resource type
	 * @param[in,out] data0 first resource data
	 * @param[in,out] data1 second resource data
	 * @param count resource data count
	 */
	void destroyResource(DestroyResourceType type,
		void* data0, void* data1 = nullptr, uint32 count = 0);
	/**
	 * @brief Actually destroys unused GPU resources.
	 */
	virtual void flushDestroyBuffer() = 0;

	/**
	 * @brief Creates and initializes a new graphics API instance.
	 */
	static void initialize(GraphicsBackend backendType, const string& appName, 
		const string& appDataName, Version appVersion, uint2 windowSize, uint32 threadCount, 
		bool useVsync, bool useTripleBuffering, bool isFullscreen);
	/**
	 * @brief Terminates and destroys graphics API instance.
	 */
	static void terminate();

	/**
	 * @brief Stores shader pipeline cache to the disk.
	 */
	virtual void storePipelineCache() { }

	/**
	 * @brief Is graphics API initialized.
	 */
	inline static bool isInitialized() noexcept { return apiInstance; }
	/**
	 * @brief Returns graphics API instance.
	 */
	inline static GraphicsAPI* get() noexcept
	{
		GARDEN_ASSERT(apiInstance);
		return apiInstance;
	}
};

} // namespace garden
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

/*******************************************************************************************************************
 * @file
 * @brief Common graphics API functions.
 */

#pragma once
#include "garden/hash.hpp"
#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

namespace garden::graphics
{

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
class GraphicsAPI final
{
public:
	/**
	 * @brief Minimal DLSS output size
	 */
	static constexpr uint8 minFramebufferSize = 32;

	enum class DestroyResourceType : uint32
	{
		DescriptorSet, Pipeline, DescriptorPool, DescriptorSetLayout,
		Sampler, Framebuffer, ImageView, Image, Buffer, Count
	};
	struct DestroyResource final
	{
		void* data0 = nullptr;
		void* data1 = nullptr;
		DestroyResourceType type = {};
		uint32 count = 0;
	};

	inline static string appDataName = {};
	inline static Version appVersion = {};
	inline static void* window = nullptr;
	inline static LinearPool<Buffer> bufferPool = {};
	inline static LinearPool<Image> imagePool = {};
	inline static LinearPool<ImageView> imageViewPool = {};
	inline static LinearPool<Framebuffer> framebufferPool = {};
	inline static LinearPool<GraphicsPipeline> graphicsPipelinePool = {};
	inline static LinearPool<ComputePipeline> computePipelinePool = {};
	inline static LinearPool<DescriptorSet> descriptorSetPool = {};
	inline static uint64 graphicsPipelineVersion = 0;
	inline static uint64 computePipelineVersion = 0;
	inline static uint64 bufferVersion = 0;
	inline static uint64 imageVersion = 0;
	inline static vector<DestroyResource> destroyBuffers[frameLag + 1];
	inline static map<void*, uint64> renderPasses = {};
	inline static CommandBuffer frameCommandBuffer = {};
	inline static CommandBuffer graphicsCommandBuffer = {};
	inline static CommandBuffer transferCommandBuffer = {};
	inline static CommandBuffer computeCommandBuffer = {};
	inline static CommandBuffer* currentCommandBuffer = nullptr;
	inline static bool isDeviceIntegrated = false;
	inline static bool isRunning = false;
	inline static uint8 fillDestroyIndex = 0;
	inline static uint8 flushDestroyIndex = 1;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	inline static bool recordGpuTime = false;
	#endif

	inline static void destroyResource(DestroyResourceType type,
		void* data0, void* data1 = nullptr, uint32 count = 0)
	{
		DestroyResource destroyResource;
		destroyResource.data0 = data0;
		destroyResource.data1 = data1;
		destroyResource.type = type;
		destroyResource.count = count;
		destroyBuffers[fillDestroyIndex].push_back(destroyResource);
	}
};

} // namespace garden::graphics
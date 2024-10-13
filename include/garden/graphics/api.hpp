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

using namespace garden;

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
	static constexpr int32 minFramebufferSize = 32;

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

	static string appDataName;
	static Version appVersion;
	static void* window;
	static LinearPool<Buffer> bufferPool;
	static LinearPool<Image> imagePool;
	static LinearPool<ImageView> imageViewPool;
	static LinearPool<Framebuffer> framebufferPool;
	static LinearPool<GraphicsPipeline> graphicsPipelinePool;
	static LinearPool<ComputePipeline> computePipelinePool;
	static LinearPool<DescriptorSet> descriptorSetPool;
	static uint64 graphicsPipelineVersion;
	static uint64 computePipelineVersion;
	static uint64 bufferVersion;
	static uint64 imageVersion;
	static vector<DestroyResource> destroyBuffers[frameLag + 1];
	static map<void*, uint64> renderPasses;
	static CommandBuffer frameCommandBuffer;
	static CommandBuffer graphicsCommandBuffer;
	static CommandBuffer transferCommandBuffer;
	static CommandBuffer computeCommandBuffer;
	static CommandBuffer* currentCommandBuffer;
	static bool isDeviceIntegrated;
	static bool isRunning;
	static uint8 fillDestroyIndex;
	static uint8 flushDestroyIndex;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	static bool recordGpuTime;
	#endif

	static void destroyResource(DestroyResourceType type,
		void* data0, void* data1 = nullptr, uint32 count = 0);
};

} // namespace garden::graphics
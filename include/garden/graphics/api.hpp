//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/pipeline/compute.hpp"
#include "garden/graphics/pipeline/graphics.hpp"

namespace garden::graphics
{

//--------------------------------------------------------------------------------------------------
class GraphicsAPI final
{
public:
	enum class DestroyResourceType : uint32
	{
		DescriptorSet, Pipeline, DescriptorPool, DescriptorSetLayout,
		Sampler, Framebuffer, ImageView, Image, Buffer, Count
	};
	struct DestroyResource final
	{
		void* data0 = nullptr;
		void* data1 = nullptr;
		void* data2 = nullptr;
		DestroyResourceType type = {};
		uint32 count = 0;
	};

	static void* hashState;
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
	static vector<DestroyResource> destroyBuffer;
	static map<void*, uint64> renderPasses;
	static CommandBuffer frameCommandBuffer;
	static CommandBuffer graphicsCommandBuffer;
	static CommandBuffer transferCommandBuffer;
	static CommandBuffer computeCommandBuffer;
	static CommandBuffer* currentCommandBuffer;
	static bool isDeviceIntegrated;
	static bool isRunning;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	static bool recordGpuTime;
	#endif

	static void destroyResource(GraphicsAPI::DestroyResourceType type, void* data0,
		void* data1 = nullptr, void* data2 = nullptr, uint32 count = 0)
	{
		GraphicsAPI::DestroyResource destroyResource;
		destroyResource.data0 = data0;
		destroyResource.data1 = data1;
		destroyResource.data2 = data2;
		destroyResource.type = type;
		destroyResource.count = count;
		GraphicsAPI::destroyBuffer.push_back(destroyResource);
	}
};

} // garden::graphics
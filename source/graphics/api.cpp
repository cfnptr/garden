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

#include "garden/graphics/api.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace std;
using namespace garden::graphics;

void* GraphicsAPI::hashState = nullptr;
void* GraphicsAPI::window = nullptr;
LinearPool<Buffer> GraphicsAPI::bufferPool;
LinearPool<Image> GraphicsAPI::imagePool;
LinearPool<ImageView> GraphicsAPI::imageViewPool;
LinearPool<Framebuffer> GraphicsAPI::framebufferPool;
LinearPool<GraphicsPipeline> GraphicsAPI::graphicsPipelinePool;
LinearPool<ComputePipeline> GraphicsAPI::computePipelinePool;
LinearPool<DescriptorSet> GraphicsAPI::descriptorSetPool;
uint64 GraphicsAPI::graphicsPipelineVersion = 0;
uint64 GraphicsAPI::computePipelineVersion = 0;
uint64 GraphicsAPI::bufferVersion = 0;
uint64 GraphicsAPI::imageVersion = 0;
vector<GraphicsAPI::DestroyResource> GraphicsAPI::destroyBuffers[];
map<void*, uint64> GraphicsAPI::renderPasses;
CommandBuffer GraphicsAPI::frameCommandBuffer;
CommandBuffer GraphicsAPI::graphicsCommandBuffer;
CommandBuffer GraphicsAPI::transferCommandBuffer;
CommandBuffer GraphicsAPI::computeCommandBuffer;
CommandBuffer* GraphicsAPI::currentCommandBuffer = nullptr;
bool GraphicsAPI::isDeviceIntegrated = false;
bool GraphicsAPI::isRunning = false;
uint8 GraphicsAPI::fillDestroyIndex = 0;
uint8 GraphicsAPI::flushDestroyIndex = 1;

#if GARDEN_DEBUG || GARDEN_EDITOR
bool GraphicsAPI::recordGpuTime = false;
#endif

//--------------------------------------------------------------------------------------------------
void GraphicsAPI::destroyResource(DestroyResourceType type, void* data0, void* data1, uint32 count)
{
	DestroyResource destroyResource;
	destroyResource.data0 = data0;
	destroyResource.data1 = data1;
	destroyResource.type = type;
	destroyResource.count = count;
	destroyBuffers[fillDestroyIndex].push_back(destroyResource);
}
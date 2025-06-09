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
 * @brief Vulkan rendering command buffer functions.
 */

#pragma once
#include "garden/graphics/command-buffer.hpp"
#include "garden/graphics/vulkan/api.hpp"

namespace garden::graphics
{

/**
 * @brief Vulkan Rendering commands recorder.
 */
class VulkanCommandBuffer final : public CommandBuffer
{
public:
	vk::CommandBuffer instance;
	vk::Fence fence;

	void addDescriptorSetBarriers(VulkanAPI* vulkanAPI, 
		const DescriptorSet::Range* descriptorSetRange, uint32 rangeCount);
	void addRenderPassBarriers(psize offset);
	void processPipelineBarriers();

	void processCommand(const BufferBarrierCommand& command) final;
	void processCommand(const BeginRenderPassCommand& command) final;
	void processCommand(const NextSubpassCommand& command) final;
	void processCommand(const ExecuteCommand& command) final;
	void processCommand(const EndRenderPassCommand& command) final;
	void processCommand(const ClearAttachmentsCommand& command) final;
	void processCommand(const BindPipelineCommand& command) final;
	void processCommand(const BindDescriptorSetsCommand& command) final;
	void processCommand(const PushConstantsCommand& command) final;
	void processCommand(const SetViewportCommand& command) final;
	void processCommand(const SetScissorCommand& command) final;
	void processCommand(const SetViewportScissorCommand& command) final;
	void processCommand(const DrawCommand& command) final;
	void processCommand(const DrawIndexedCommand& command) final;
	void processCommand(const DispatchCommand& command) final;
	void processCommand(const FillBufferCommand& command) final;
	void processCommand(const CopyBufferCommand& command) final;
	void processCommand(const ClearImageCommand& command) final;
	void processCommand(const CopyImageCommand& command) final;
	void processCommand(const CopyBufferImageCommand& command) final;
	void processCommand(const BlitImageCommand& command) final;
	void processCommand(const SetDepthBiasCommand& command) final;
	void processCommand(const BuildAccelerationStructureCommand& command) final;
	void processCommand(const TraceRaysCommand& command) final;

	#if GARDEN_DEBUG
	void processCommand(const BeginLabelCommand& command) final;
	void processCommand(const EndLabelCommand& command) final;
	void processCommand(const InsertLabelCommand& command) final;
	#endif
public:
	VulkanCommandBuffer(VulkanAPI* vulkanAPI, CommandBufferType type);
	~VulkanCommandBuffer() final;

	void submit() final;
	bool isBusy() final;
};

} // namespace garden::graphics
// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
	VulkanAPI* vulkanAPI = nullptr;
	vk::CommandBuffer instance;
	vk::Fence fence;

	static void addBufferBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState& newBufferState, 
		ID<Buffer> buffer, uint64 size = VK_WHOLE_SIZE, uint64 offset = 0);
	static void addImageBarrier(VulkanAPI* vulkanAPI, 
		Image::LayoutState& newImageState, ID<ImageView> imageView);
	static void addDescriptorSetBarriers(VulkanAPI* vulkanAPI, 
		const DescriptorSet::Range* descriptorSetRanges, uint32 rangeCount);

	void addRenderPassBarriers(const Command* command);
	void addRenderPassBarriers(uint32 thisSize);
	void addRenderPassBarriersAsync(uint32 thisSize);
	void processPipelineBarriers();

	void processCommand(const BufferBarrierCommand& command) override;
	void processCommand(const BeginRenderPassCommand& command) override;
	void processCommand(const ExecuteCommand& command) override;
	void processCommand(const EndRenderPassCommand& command) override;
	void processCommand(const ClearAttachmentsCommand& command) override;
	void processCommand(const BindPipelineCommand& command) override;
	void processCommand(const BindDescriptorSetsCommand& command) override;
	void processCommand(const PushConstantsCommand& command) override;
	void processCommand(const SetViewportCommand& command) override;
	void processCommand(const SetScissorCommand& command) override;
	void processCommand(const SetViewportScissorCommand& command) override;
	void processCommand(const SetDepthBiasCommand& command) override;
	void processCommand(const DrawCommand& command) override;
	void processCommand(const DrawIndexedCommand& command) override;
	void processCommand(const DispatchCommand& command) override;
	void processCommand(const FillBufferCommand& command) override;
	void processCommand(const CopyBufferCommand& command) override;
	void processCommand(const ClearImageCommand& command) override;
	void processCommand(const CopyImageCommand& command) override;
	void processCommand(const CopyBufferImageCommand& command) override;
	void processCommand(const BlitImageCommand& command) override;
	
	void processCommand(const BuildAccelerationStructureCommand& command) override;
	void processCommand(const CopyAccelerationStructureCommand& command) override;
	void processCommand(const TraceRaysCommand& command) override;
	void processCommand(const CustomRenderCommand& command) override;

	#if GARDEN_DEBUG
	void processCommand(const BeginLabelCommand& command) override;
	void processCommand(const EndLabelCommand& command) override;
	void processCommand(const InsertLabelCommand& command) override;
	#endif

	VulkanCommandBuffer(VulkanAPI* vulkanAPI, CommandBufferType type);
	~VulkanCommandBuffer() override;

	void submit() override;
	bool isBusy() override;

	static constexpr uint64 writeAccessMask =
		VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | 
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | 
		VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR | 
		VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_TILE_ATTACHMENT_WRITE_BIT_QCOM | 
		VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | 
		VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT | VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT | 
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT | 
		VK_ACCESS_2_OPTICAL_FLOW_WRITE_BIT_NV | VK_ACCESS_2_DATA_GRAPH_WRITE_BIT_ARM | 
		0x100000000000000ULL; // TODO: not wide spread yet: VK_ACCESS_2_MEMORY_DECOMPRESSION_WRITE_BIT_EXT;

	static constexpr bool isDifferentState(Image::LayoutState oldState, Image::LayoutState newState) noexcept
	{
		auto isWriteAccess = (oldState.access & writeAccessMask);
		return (!isWriteAccess && oldState.stage != newState.stage) || 
			oldState.layout != newState.layout || isWriteAccess;
	}
	static constexpr bool isDifferentState(Buffer::BarrierState oldState, Buffer::BarrierState newState) noexcept
	{
		auto isWriteAccess = (oldState.access & writeAccessMask);
		return (!isWriteAccess && oldState.stage != newState.stage) || isWriteAccess;
	}
};

} // namespace garden::graphics
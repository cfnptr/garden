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

	static void addBufferBarrier(VulkanAPI* vulkanAPI, Buffer::BarrierState newBufferState, 
		ID<Buffer> buffer, uint64 size = VK_WHOLE_SIZE, uint64 offset = 0);
	static void addDescriptorSetBarriers(VulkanAPI* vulkanAPI, 
		const DescriptorSet::Range* descriptorSetRange, uint32 rangeCount);
	void addRenderPassBarriers(VulkanAPI* vulkanAPI, psize offset);
	void processPipelineBarriers(VulkanAPI* vulkanAPI);

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
	void processCommand(const CopyAccelerationStructureCommand& command) final;
	void processCommand(const TraceRaysCommand& command) final;
	void processCommand(const CustomRenderCommand& command) final;

	#if GARDEN_DEBUG
	void processCommand(const BeginLabelCommand& command) final;
	void processCommand(const EndLabelCommand& command) final;
	void processCommand(const InsertLabelCommand& command) final;
	#endif

	VulkanCommandBuffer(VulkanAPI* vulkanAPI, CommandBufferType type);
	~VulkanCommandBuffer() final;

	void submit() final;
	bool isBusy() final;

	static constexpr uint32 writeAccessMask =
		VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
		VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_EXT;

	static constexpr bool isDifferentState(const Image::BarrierState& oldState, const Image::BarrierState& newState) noexcept
	{
		auto layoutTransition = false;
		switch (newState.stage)
		{
			case (uint32)vk::PipelineStageFlagBits::eFragmentShader: layoutTransition = oldState.fragLayoutTrans; break;
			case (uint32)vk::PipelineStageFlagBits::eTransfer: layoutTransition = oldState.transLayoutTrans; break;
			case (uint32)vk::PipelineStageFlagBits::eComputeShader: layoutTransition = oldState.compLayoutTrans; break;
			case (uint32)vk::PipelineStageFlagBits::eRayTracingShaderKHR: layoutTransition = oldState.rtLayoutTrans; break;
			default: layoutTransition = false; break; // TODO: other stages.
		}
		return oldState.layout != newState.layout || (oldState.access & writeAccessMask) || layoutTransition;
	}
	static constexpr bool isDifferentState(const Buffer::BarrierState& oldState) noexcept
	{
		return oldState.access & writeAccessMask;
	}

	static constexpr void updateLayoutTrans(const Image::BarrierState& oldState, Image::BarrierState& newState) noexcept
	{
		if (oldState.layout != newState.layout)
		{
			newState.fragLayoutTrans = newState.transLayoutTrans = newState.compLayoutTrans = newState.rtLayoutTrans = 0;
			switch (newState.stage)
			{
				case (uint32)vk::PipelineStageFlagBits::eFragmentShader:
					newState.transLayoutTrans = newState.compLayoutTrans = newState.rtLayoutTrans = 1; break;
				case (uint32)vk::PipelineStageFlagBits::eTransfer:
					newState.fragLayoutTrans = newState.compLayoutTrans = newState.rtLayoutTrans = 1; break;
				case (uint32)vk::PipelineStageFlagBits::eComputeShader:
					newState.fragLayoutTrans = newState.transLayoutTrans = newState.rtLayoutTrans = 1; break;
				case (uint32)vk::PipelineStageFlagBits::eRayTracingShaderKHR:
					newState.fragLayoutTrans = newState.transLayoutTrans = newState.compLayoutTrans = 1; break;
				// TODO: other stages.
			}
			return;
		}

		newState.fragLayoutTrans = oldState.fragLayoutTrans;
		newState.transLayoutTrans = oldState.transLayoutTrans;
		newState.compLayoutTrans = oldState.compLayoutTrans;
		newState.rtLayoutTrans = oldState.rtLayoutTrans;

		switch (newState.stage)
		{
			case (uint32)vk::PipelineStageFlagBits::eFragmentShader: newState.fragLayoutTrans = 0; break;
			case (uint32)vk::PipelineStageFlagBits::eTransfer: newState.transLayoutTrans = 0; break;
			case (uint32)vk::PipelineStageFlagBits::eComputeShader: newState.compLayoutTrans = 0; break;
			case (uint32)vk::PipelineStageFlagBits::eRayTracingShaderKHR: newState.rtLayoutTrans = 0; break;
			// TODO: other stages.
		}
	}
};

} // namespace garden::graphics
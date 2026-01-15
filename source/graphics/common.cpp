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

#include "garden/graphics/common.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace std;
using namespace math;
using namespace garden::graphics;

#if GARDEN_DEBUG
//**********************************************************************************************************************
void DebugLabel::begin(const string& name, Color color, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT_MSG(graphicsAPI->currentCommandBuffer, "Assert " + name);

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	if (threadIndex < 0)
	{
		GARDEN_ASSERT_MSG(!graphicsAPI->isCurrentRenderPassAsync, "Assert " + name);
		BeginLabelCommand command;
		command.color = color;
		command.name = name.c_str();
		currentCommandBuffer->addCommand(command, threadIndex);
	}
	else
	{
		GARDEN_ASSERT_MSG(graphicsAPI->isCurrentRenderPassAsync, "Assert " + name);
		graphicsAPI->calcAutoThreadIndex(threadIndex);
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			array<float, 4> values; *(float4*)values.data() = (float4)color;
			vk::DebugUtilsLabelEXT debugLabel(name.c_str(), values);
			VulkanAPI::get()->secondaryCommandBuffers[threadIndex].beginDebugUtilsLabelEXT(debugLabel);
		}
		else abort();
	}
}
void DebugLabel::end(int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT(graphicsAPI->currentCommandBuffer);

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	if (threadIndex < 0)
	{
		GARDEN_ASSERT(!graphicsAPI->isCurrentRenderPassAsync);
		currentCommandBuffer->addCommand(EndLabelCommand(), threadIndex);
	}
	else
	{
		GARDEN_ASSERT(graphicsAPI->isCurrentRenderPassAsync);
		graphicsAPI->calcAutoThreadIndex(threadIndex);
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
			VulkanAPI::get()->secondaryCommandBuffers[threadIndex].endDebugUtilsLabelEXT();
		else abort();
	}
}
void DebugLabel::insert(const string& name, Color color, int32 threadIndex)
{
	auto graphicsAPI = GraphicsAPI::get();
	GARDEN_ASSERT(!name.empty());

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	if (threadIndex < 0)
	{
		GARDEN_ASSERT_MSG(!graphicsAPI->isCurrentRenderPassAsync, "Assert " + name);
		InsertLabelCommand command;
		command.color = color;
		command.name = name.c_str();
		currentCommandBuffer->addCommand(command, threadIndex);
	}
	else
	{
		GARDEN_ASSERT_MSG(graphicsAPI->isCurrentRenderPassAsync, "Assert " + name);
		graphicsAPI->calcAutoThreadIndex(threadIndex);
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			array<float, 4> values; *(float4*)values.data() = (float4)color;
			vk::DebugUtilsLabelEXT debugLabel(name.c_str(), values);
			VulkanAPI::get()->secondaryCommandBuffers[threadIndex].insertDebugUtilsLabelEXT(debugLabel);
		}
		else abort();
	}	
}
#endif
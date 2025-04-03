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

#include "garden/editor/system/graphics.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/vulkan/api.hpp"
#include "garden/file.hpp"
#include "mpio/os.hpp"

using namespace garden;

//**********************************************************************************************************************
GraphicsEditorSystem::GraphicsEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", GraphicsEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", GraphicsEditorSystem::deinit);
}
GraphicsEditorSystem::~GraphicsEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", GraphicsEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", GraphicsEditorSystem::deinit);

		delete[] gpuSortedBuffer;
		delete[] cpuSortedBuffer;
		delete[] gpuFpsBuffer;
		delete[] cpuFpsBuffer;
	}
}

void GraphicsEditorSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", GraphicsEditorSystem::preUiRender);
	ECSM_SUBSCRIBE_TO_EVENT("EditorBarTool", GraphicsEditorSystem::editorBarTool);
}
void GraphicsEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", GraphicsEditorSystem::preUiRender);
		ECSM_UNSUBSCRIBE_FROM_EVENT("EditorBarTool", GraphicsEditorSystem::editorBarTool);
	}
}

//**********************************************************************************************************************
static void updateHistogram(const char* name, float* sampleBuffer, float* sortedBuffer, float deltaTime)
{
	auto minValue = (float)FLT_MAX;
	auto maxValue = (float)-FLT_MAX;
	auto average = 0.0f;

	for (uint32 i = 0; i < GraphicsEditorSystem::sampleBufferSize; i++)
	{
		auto sample = sampleBuffer[i];
		if (i > 0)
			sampleBuffer[i - 1] = sampleBuffer[i];
		if (sample < minValue)
			minValue = sample;
		if (sample > maxValue)
			maxValue = sample;
		average += sample;
	}

	average /= GraphicsEditorSystem::sampleBufferSize;
	auto fps = 1.0f / deltaTime;
	sampleBuffer[GraphicsEditorSystem::sampleBufferSize - 1] = fps;

	memcpy(sortedBuffer, sampleBuffer, GraphicsEditorSystem::sampleBufferSize * sizeof(float));
	std::sort(sortedBuffer, sortedBuffer + GraphicsEditorSystem::sampleBufferSize, std::less<float>());

	auto onePercentLow = 0.0f;
	for (uint32 i = 0; i < GraphicsEditorSystem::sampleBufferSize / 100; i++)
		onePercentLow += sortedBuffer[i];
	onePercentLow /= GraphicsEditorSystem::sampleBufferSize / 100;

	ImGui::Text("Time: %f | 1%% Low: %f",
		1.0f / average, 1.0f / onePercentLow);
	ImGui::Text("FPS: %d | 1%% Low: %d | Minimal: %d",
		(int)average, (int)onePercentLow, (int)minValue);
	ImGui::PlotHistogram(name, sampleBuffer, GraphicsEditorSystem::sampleBufferSize,
		0, nullptr, minValue, maxValue, { 256.0f, 64.0f }, 4);
}

//**********************************************************************************************************************
void GraphicsEditorSystem::showPerformanceStats()
{
	// TODO: show triangle and primitive draw count.

	if (ImGui::Begin("Performance Stats", &performanceStats, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!cpuFpsBuffer)
		{
			cpuFpsBuffer = new float[GraphicsEditorSystem::sampleBufferSize]();
			gpuFpsBuffer = new float[GraphicsEditorSystem::sampleBufferSize]();
			cpuSortedBuffer = new float[GraphicsEditorSystem::sampleBufferSize]();
			gpuSortedBuffer = new float[GraphicsEditorSystem::sampleBufferSize]();
		}

		ImGui::SeparatorText("Mesh Draw Calls");
		ImGui::Text("Total:      "); ImGui::SameLine();
		auto drawCount = opaqueDrawCount + translucentDrawCount;
		auto totalCount = opaqueTotalCount + translucentTotalCount;
		auto progressInfo = to_string(drawCount) + " / " + to_string(totalCount);
		auto fraction = totalCount > 0 ? (float)drawCount / totalCount : 0.0f;
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), progressInfo.c_str());

		ImGui::Text("Opaque:     "); ImGui::SameLine();
		progressInfo = to_string(opaqueDrawCount) +
			" / " + to_string(opaqueTotalCount);
		fraction = opaqueTotalCount > 0 ? (float)opaqueDrawCount / opaqueTotalCount : 0.0f;
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), progressInfo.c_str());

		ImGui::Text("Translucent:"); ImGui::SameLine();
		progressInfo = to_string(translucentDrawCount) +
			" / " + to_string(translucentTotalCount);
		fraction = translucentTotalCount > 0 ?
			(float)translucentDrawCount / translucentTotalCount : 0.0f;
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), progressInfo.c_str());

		ImGui::SeparatorText("Frames Per Second");
		auto inputSystem = InputSystem::Instance::get();
		auto deltaTime = (float)inputSystem->getDeltaTime() / (float)inputSystem->timeMultiplier;
		updateHistogram("CPU", cpuFpsBuffer, cpuSortedBuffer, deltaTime);
		ImGui::Spacing();

		auto graphicsAPI = GraphicsAPI::get();
		if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto vulkanAPI = VulkanAPI::get();
			auto swapchainBuffer = vulkanAPI->vulkanSwapchain->getCurrentVkBuffer();
			uint64 timestamps[2]; timestamps[0] = 0; timestamps[1] = 0;

			auto vkResult = vk::Result::eNotReady;
			if (swapchainBuffer->isPoolClean)
			{
				vkResult = vulkanAPI->device.getQueryPoolResults(
					swapchainBuffer->queryPool, 0, 2, sizeof(uint64) * 2, timestamps,
					(vk::DeviceSize)sizeof(uint64), vk::QueryResultFlagBits::e64);
			}

			if (vkResult == vk::Result::eSuccess)
			{
				auto difference = (double)(timestamps[1] - timestamps[0]) *
					(double)vulkanAPI->deviceProperties.properties.limits.timestampPeriod;
				difference /= 1000000000.0;
				updateHistogram("GPU", gpuFpsBuffer, gpuSortedBuffer, (float)difference);
			}
		}
		else abort();

		ImGui::SeparatorText("GPU Information");
		if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto vulkanAPI = VulkanAPI::get();
			ImGui::Text("Queues: %lu, %lu, %lu. (Graphics, Transfer, Compute)",
				(unsigned long)vulkanAPI->graphicsQueueFamilyIndex,
				(unsigned long)vulkanAPI->transferQueueFamilyIndex,
				(unsigned long)vulkanAPI->computeQueueFamilyIndex);
		}
		else abort();

		auto isIntegrated = !graphicsAPI->isDeviceIntegrated;
		ImGui::Checkbox("Discrete |", &isIntegrated); ImGui::SameLine();
		ImGui::Text("Swapchain Size: %lu", (unsigned long)graphicsAPI->swapchain->getBufferCount());
		
		graphicsAPI->recordGpuTime = true;
	}
	else
	{
		GraphicsAPI::get()->recordGpuTime = false;
	}
	ImGui::End();
}

//**********************************************************************************************************************
void GraphicsEditorSystem::showMemoryStats()
{
	if (ImGui::Begin("Memory Stats", &memoryStats, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		{
			auto vulkanAPI = VulkanAPI::get();
			VmaBudget heapBudgets[VK_MAX_MEMORY_HEAPS];
			vmaGetHeapBudgets(vulkanAPI->memoryAllocator, heapBudgets);
			const VkPhysicalDeviceMemoryProperties* memoryProperties;
			vmaGetMemoryProperties(vulkanAPI->memoryAllocator, &memoryProperties);
			auto memoryHeaps = memoryProperties->memoryHeaps;

			VkDeviceSize deviceAllocationBytes = 0, deviceBlockBytes = 0,
				hostAllocationBytes = 0, hostBlockBytes = 0, usage = 0, budget = 0;
			for (uint32 i = 0; i < memoryProperties->memoryHeapCount; i++)
			{
				const auto& heapBudget = heapBudgets[i];
				usage += heapBudget.usage; budget += heapBudget.budget;

				if (memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					deviceAllocationBytes += heapBudget.statistics.allocationBytes;
					deviceBlockBytes += heapBudget.statistics.blockBytes;
				}
				else
				{
					hostAllocationBytes += heapBudget.statistics.allocationBytes;
					hostBlockBytes += heapBudget.statistics.blockBytes;
				}
			}

			ImGui::SeparatorText("GPU Memory");
			ImGui::Text("Allocated: "); ImGui::SameLine();
			auto gpuInfo =
				toBinarySizeString(deviceAllocationBytes + hostAllocationBytes) + " / " +
				toBinarySizeString(deviceBlockBytes + hostBlockBytes);
			auto fraction = deviceBlockBytes + hostBlockBytes > 0 ?
				(double)(deviceAllocationBytes + hostAllocationBytes) /
				(double)(deviceBlockBytes + hostBlockBytes) : 0.0;
			ImGui::ProgressBar((float)fraction, ImVec2(144.0f, 0.0f), gpuInfo.c_str());

			ImGui::Text("Device:    "); ImGui::SameLine();
			gpuInfo = toBinarySizeString(deviceAllocationBytes) +
				" / " + toBinarySizeString(deviceBlockBytes);
			fraction = deviceBlockBytes > 0 ?
				(double)deviceAllocationBytes / (double)deviceBlockBytes : 0.0;
			ImGui::ProgressBar((float)fraction, ImVec2(144.0f, 0.0f), gpuInfo.c_str());

			ImGui::Text("Host:      "); ImGui::SameLine();
			gpuInfo = toBinarySizeString(hostAllocationBytes) +
				" / " + toBinarySizeString(hostBlockBytes);
			fraction = hostBlockBytes > 0 ?
				(double)hostAllocationBytes / (double)hostBlockBytes : 0.0;
			ImGui::ProgressBar((float)fraction, ImVec2(144.0f, 0.0f), gpuInfo.c_str());

			ImGui::Text("Real Usage:"); ImGui::SameLine();
			gpuInfo = toBinarySizeString(usage) + " / " + toBinarySizeString(budget);
			fraction = hostBlockBytes > 0 ? (double)usage / (double)budget : 0.0;
			ImGui::ProgressBar((float)fraction, ImVec2(144.0f, 0.0f), gpuInfo.c_str());
		}
		else abort();

		ImGui::SeparatorText("OS Memory");
		ImGui::Text("RAM:       "); ImGui::SameLine();
		auto totalRamSize = mpio::OS::getTotalRamSize(), freeRamSize = mpio::OS::OS::getFreeRamSize();
		auto usedRamSize = totalRamSize - freeRamSize;
		auto gpuInfo = toBinarySizeString(usedRamSize) + " / " + toBinarySizeString(totalRamSize);
		auto fraction = totalRamSize > 0 ? (double)usedRamSize / (double)totalRamSize : 0.0;
		ImGui::ProgressBar((float)fraction, ImVec2(144.0f, 0.0f), gpuInfo.c_str());
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Descriptor Pool Usage"))
		{
			ImGui::Indent();
			ImGui::Text("Combined Samplers: %u", DescriptorSet::combinedSamplerCount);
			ImGui::Text("Uniform Buffers: %u", DescriptorSet::uniformBufferCount);
			ImGui::Text("Storage Images: %u", DescriptorSet::storageImageCount);
			ImGui::Text("Storage Buffers: %u", DescriptorSet::storageBufferCount);
			ImGui::Text("Input Attachments: %u", DescriptorSet::inputAttachmentCount);
			ImGui::Unindent();
		}

		// TODO: CPU RAM usage.
	}
	ImGui::End();
}

//**********************************************************************************************************************
void GraphicsEditorSystem::preUiRender()
{
	if (performanceStats)
		showPerformanceStats();
	if (memoryStats)
		showMemoryStats();
}
void GraphicsEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Performance Stats"))
		performanceStats = true;
	if (ImGui::MenuItem("Memory Stats (CPU/GPU)"))
		memoryStats = true;
}
#endif
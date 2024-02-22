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

#include "garden/editor/system/graphics.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#include "garden/file.hpp"

using namespace garden;

//**********************************************************************************************************************
GraphicsEditorSystem::GraphicsEditorSystem(Manager* manager,
	GraphicsSystem* graphicsSystem) : EditorSystem(manager, graphicsSystem)
{
	SUBSCRIBE_TO_EVENT("RenderEditor", GraphicsEditorSystem::renderEditor);
	SUBSCRIBE_TO_EVENT("EditorBarTool", GraphicsEditorSystem::editorBarTool);
}
GraphicsEditorSystem::~GraphicsEditorSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("RenderEditor", GraphicsEditorSystem::renderEditor);
	}

	delete[] gpuSortedBuffer;
	delete[] cpuSortedBuffer;
	delete[] gpuFpsBuffer;
	delete[] cpuFpsBuffer;
}

//**********************************************************************************************************************
static void updateHistogram(const char* name, float* sampleBuffer, float* sortedBuffer, float deltaTime)
{
	auto minValue = (float)FLT_MAX;
	auto maxValue = (float)-FLT_MAX;
	auto average = 0.0f;

	for (uint32 i = 0; i < DATA_SAMPLE_BUFFER_SIZE; i++)
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

	average /= DATA_SAMPLE_BUFFER_SIZE;
	auto fps = 1.0f / deltaTime;
	sampleBuffer[DATA_SAMPLE_BUFFER_SIZE - 1] = fps;

	memcpy(sortedBuffer, sampleBuffer, DATA_SAMPLE_BUFFER_SIZE * sizeof(float));
	std::sort(sortedBuffer, sortedBuffer + DATA_SAMPLE_BUFFER_SIZE, std::less<float>());

	auto onePercentLow = 0.0f;
	for (uint32 i = 0; i < DATA_SAMPLE_BUFFER_SIZE / 100; i++)
		onePercentLow += sortedBuffer[i];
	onePercentLow /= DATA_SAMPLE_BUFFER_SIZE / 100;

	ImGui::Text("Time: %f | 1%% Low: %f",
		1.0f / average, 1.0f / onePercentLow);
	ImGui::Text("FPS: %d | 1%% Low: %d | Minimal: %d",
		(int)average, (int)onePercentLow, (int)minValue);
	ImGui::PlotHistogram(name, sampleBuffer, DATA_SAMPLE_BUFFER_SIZE,
		0, nullptr, minValue, maxValue, { 256.0f, 64.0f }, 4);
}

//**********************************************************************************************************************
void GraphicsEditorSystem::showPerformanceStatistics()
{
	if (ImGui::Begin("Performance Statistics", &performanceStatistics, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!cpuFpsBuffer)
		{
			cpuFpsBuffer = new float[DATA_SAMPLE_BUFFER_SIZE]();
			gpuFpsBuffer = new float[DATA_SAMPLE_BUFFER_SIZE]();
			cpuSortedBuffer = new float[DATA_SAMPLE_BUFFER_SIZE]();
			gpuSortedBuffer = new float[DATA_SAMPLE_BUFFER_SIZE]();
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
		auto inputSystem = getManager()->get<InputSystem>();
		auto deltaTime = (float)inputSystem->getDeltaTime();
		updateHistogram("CPU", cpuFpsBuffer, cpuSortedBuffer, deltaTime);
		ImGui::Spacing();

		auto& swapchainBuffer = Vulkan::swapchain.getCurrentBuffer();
		uint64 timestamps[2]; timestamps[0] = 0; timestamps[1] = 0;

		auto vkResult = vk::Result::eNotReady;
		if (swapchainBuffer.isPoolClean)
		{
			vkResult = Vulkan::device.getQueryPoolResults(
				swapchainBuffer.queryPool, 0, 2, sizeof(uint64) * 2, timestamps,
				(vk::DeviceSize)sizeof(uint64), vk::QueryResultFlagBits::e64);
		}
		
		if (vkResult == vk::Result::eSuccess)
		{
			auto difference = (double)(timestamps[1] - timestamps[0]) *
				(double)Vulkan::deviceProperties.properties.limits.timestampPeriod;
			difference /= 1000000000.0;
			updateHistogram("GPU", gpuFpsBuffer, gpuSortedBuffer, (float)difference);
		}

		ImGui::SeparatorText("Device Information");
		ImGui::Text("Queue Index Graphics: %d, Transfer: %d, Compute: %d",
			Vulkan::graphicsQueueFamilyIndex,
			Vulkan::transferQueueFamilyIndex,
			Vulkan::computeQueueFamilyIndex);
		auto isIntegrated = !GraphicsAPI::isDeviceIntegrated;
		ImGui::Checkbox("Discrete |", &isIntegrated); ImGui::SameLine();
		ImGui::Text("Swapchain Size: %d", (int)Vulkan::swapchain.getBufferCount());
		
		GraphicsAPI::recordGpuTime = true;
	}
	else
	{
		GraphicsAPI::recordGpuTime = false;
	}
	ImGui::End();
}

//**********************************************************************************************************************
void GraphicsEditorSystem::showMemoryStatistics()
{
	if (ImGui::Begin("Memory Statistics", &memoryStatistics, ImGuiWindowFlags_AlwaysAutoResize))
	{
		// TODO: CPU RAM usage.

		VmaBudget heapBudgets[VK_MAX_MEMORY_HEAPS];
		vmaGetHeapBudgets(Vulkan::memoryAllocator, heapBudgets);
		const VkPhysicalDeviceMemoryProperties* memoryProperties;
		vmaGetMemoryProperties(Vulkan::memoryAllocator, &memoryProperties);
		auto memoryHeaps = memoryProperties->memoryHeaps;

		VkDeviceSize deviceAllocationBytes = 0, deviceBlockBytes = 0,
			hostAllocationBytes = 0, hostBlockBytes = 0, usage = 0, budget = 0;
		for (uint32 i = 0; i < memoryProperties->memoryHeapCount; i++)
		{
			auto& heapBudget = heapBudgets[i];
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
	ImGui::End();
}

//**********************************************************************************************************************
void GraphicsEditorSystem::renderEditor()
{
	if (performanceStatistics)
		showPerformanceStatistics();
	if (memoryStatistics)
		showMemoryStatistics();
}
void GraphicsEditorSystem::editorBarTool()
{
	if (ImGui::MenuItem("Performance Statistics"))
		performanceStatistics = true;
	if (ImGui::MenuItem("Memory Statistics"))
		memoryStatistics = true;
}
#endif
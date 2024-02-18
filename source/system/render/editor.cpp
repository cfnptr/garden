//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/glfw.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/graphics/imgui-impl.hpp"
#include "garden/system/render/fxaa.hpp"
#include "garden/system/render/deferred.hpp" // TODO: remove?
#include "garden/system/render/editor/resource.hpp"
#include "garden/system/render/editor/hierarchy.hpp"

#include "mpio/os.hpp"
#include "mpio/directory.hpp"

using namespace mpio;
using namespace garden;

// TODO: split to several files.

//--------------------------------------------------------------------------------------------------
EditorRenderSystem::~EditorRenderSystem()
{
	delete[] gpuSortedBuffer;
	delete[] cpuSortedBuffer;
	delete[] gpuFpsBuffer;
	delete[] cpuFpsBuffer;
}

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showMainMenuBar()
{
	auto manager = getManager();
	auto graphicsSystem = getGraphicsSystem();
	if (graphicsSystem->getCursorMode() == CursorMode::Locked)
		return;

	ImGui::BeginMainMenuBar();
	if (ImGui::BeginMenu("Garden"))
	{
		if (ImGui::MenuItem("About"))
			aboutWindow = true;
		if (ImGui::MenuItem("Options"))
			optionsWindow = true;
		if (ImGui::MenuItem("ImGui Demo"))
			demoWindow = true;
		if (ImGui::MenuItem("Exit"))
			glfwSetWindowShouldClose((GLFWwindow*)GraphicsAPI::window, GLFW_TRUE);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("File"))
	{
		if (manager->has<TransformSystem>())
		{
			if (ImGui::MenuItem("New Scene"))
				newScene = true;
			if (ImGui::MenuItem("Export Scene"))
				exportScene = true;
		}
		else if (barFiles.empty())
		{
			ImGui::TextDisabled("Nothing here");
		}

		if (!barFiles.empty())
			for (auto onBarFile : barFiles)
				onBarFile();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Create"))
	{
		if (!barCreates.empty())
		{
			for (auto onBarCreate : barCreates)
				onBarCreate();
		}
		else
		{
			ImGui::TextDisabled("Nothing here");
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Tools"))
	{
		for (auto onBarTool : barTools)
			onBarTool();
		if (ImGui::MenuItem("Performance Statistics"))
			performanceStatistics = true;
		if (ImGui::MenuItem("Memory Statistics"))
			memoryStatistics = true;
		ImGui::EndMenu();
	}

	auto threadSystem = manager->tryGet<ThreadSystem>();

	uint32 taskCount = 0;
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getBackgroundPool();
		taskCount = threadPool.getPendingTaskCount();
	}
	
	auto stats = "[S: " + to_string((uint32)manager->getSystems().size()) +
		" | E: " + to_string(manager->getEntities().getCount()) +
		" | T: " + to_string(taskCount) + "]";
	auto textSize = ImGui::CalcTextSize(stats.c_str());
	ImGui::SameLine(ImGui::GetWindowWidth() - (textSize.x + 16.0f));
	ImGui::Text("%s", stats.c_str());
	ImGui::EndMainMenuBar();
}

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showAboutWindow()
{
	if (ImGui::Begin("About Garden", &aboutWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Creator: Nikita Fediuchin");
		ImGui::Text("Version: " GARDEN_VERSION_STRING);

		if (ImGui::CollapsingHeader("PC"))
		{
			ImGui::Text("OS: " GARDEN_OS_NAME " (" GARDEN_ARCH ")");
			ImGui::Text("SIMDs: %s", GARDEN_SIMD_STRING);
			auto cpuName = OS::getCpuName();
			ImGui::Text("CPU: %s", cpuName.c_str());
			auto ramString = toBinarySizeString(OS::getTotalRamSize());
			ImGui::Text("RAM: %s", ramString.c_str());

			ImGui::Text("GPU: %s", Vulkan::deviceProperties.properties.deviceName.data());
			auto apiVersion = Vulkan::deviceProperties.properties.apiVersion;
			auto apiString = to_string(VK_API_VERSION_MAJOR(apiVersion)) + "." +
				to_string(VK_API_VERSION_MINOR(apiVersion)) + "." +
				to_string(VK_API_VERSION_PATCH(apiVersion));
			ImGui::Text("Vulkan API: %s", apiString.c_str());
		}
	}
	ImGui::End();
}

//--------------------------------------------------------------------------------------------------
static void getFileInfo(const fs::path& path, int& fileCount, uint64& binarySize)
{
	auto iterator = fs::directory_iterator(path);
	for (auto& entry : iterator)
	{
		if (entry.is_directory())
		{
			getFileInfo(entry.path(), fileCount, binarySize);
			continue;
		}
		
		if (!entry.is_regular_file())
			continue;

		binarySize += (uint64)entry.file_size();
		fileCount++;
	}
}
void EditorRenderSystem::showOptionsWindow()
{
	if (ImGui::Begin("Options", &optionsWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto manager = getManager();
		auto graphicsSystem = getGraphicsSystem();

		if (ImGui::Checkbox("V-Sync", &graphicsSystem->useVsync))
		{
			auto settingsSystem = manager->tryGet<SettingsSystem>();
			if (settingsSystem)
				settingsSystem->setBool("useVsync", graphicsSystem->useVsync);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Triple Buffering", &graphicsSystem->useTripleBuffering);

		auto deferredSystem = manager->tryGet<DeferredRenderSystem>();
		auto fxaaSystem = manager->tryGet<FxaaRenderSystem>();
		if (fxaaSystem && deferredSystem)
		{
			ImGui::SameLine();
			if (ImGui::Checkbox("FXAA", &fxaaSystem->isEnabled))
			{
				deferredSystem->runSwapchainPass = !fxaaSystem->isEnabled;
				auto settingsSystem = manager->tryGet<SettingsSystem>();
				if (settingsSystem)
					settingsSystem->setBool("useFXAA", fxaaSystem->isEnabled);
			}
		}

		const auto renderScaleTypes = " 50%\0 75%\0 100%\0 150%\0 200%\0\0";
		if (deferredSystem && ImGui::Combo("Render Scale", renderScaleType, renderScaleTypes))
		{
			float renderScale;
			switch (renderScaleType)
			{
			case 0: renderScale = 0.50f; break;
			case 1: renderScale = 0.75f; break;
			case 2: renderScale = 1.0f; break;
			case 3: renderScale = 1.5f; break;
			case 4: renderScale = 2.0f; break;
			default: abort();
			}
			
			deferredSystem->setRenderScale(renderScale);
			auto settingsSystem = manager->tryGet<SettingsSystem>();
			if (settingsSystem)
				settingsSystem->setFloat("renderScale", renderScale);
		}
		ImGui::Spacing();

		auto cachePath = Directory::getAppDataPath(
			GARDEN_APP_NAME_LOWERCASE_STRING) / "caches";
		int fileCount = 0; uint64 binarySize = 0;
		if (fs::exists(cachePath))
			getFileInfo(cachePath, fileCount, binarySize);
		auto sizeString = toBinarySizeString(binarySize);
		ImGui::Text("Application cache: %d files, %s", fileCount, sizeString.c_str());

		fileCount = 0; binarySize = 0;
		if (fs::exists(GARDEN_CACHES_PATH))
			getFileInfo(GARDEN_CACHES_PATH, fileCount, binarySize);
		sizeString = toBinarySizeString(binarySize);
		ImGui::Text("Project cache: %d files, %s", fileCount, sizeString.c_str());

		if (ImGui::Button("Clear application cache", ImVec2(-FLT_MIN, 0.0f)))
			fs::remove_all(cachePath);
		if (ImGui::Button("Clear project cache", ImVec2(-FLT_MIN, 0.0f)))
			fs::remove_all(GARDEN_CACHES_PATH);
	}
	ImGui::End();
}

//--------------------------------------------------------------------------------------------------
static void updateHistogram(const char* name,
	float* sampleBuffer, float* sortedBuffer, float deltaTime)
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

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showPerformanceStatistics()
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
		auto graphicsSystem = getGraphicsSystem();
		auto deltaTime = (float)graphicsSystem->getDeltaTime();
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

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showMemoryStatistics()
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

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showEntityInspector()
{
	auto showEntityInspector = true;
	if (ImGui::Begin("Entity Inspector", &showEntityInspector,
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto manager = getManager();
		ImGui::Text("ID: %d | Components: %d ", *selectedEntity,
			manager->getComponentCount(selectedEntity));

		if (!manager->has<DoNotDestroyComponent>(selectedEntity))
		{
			ImGui::SameLine();
			auto cursorPos = ImGui::GetCursorPos();
			cursorPos.y -= 4.0f;
			ImGui::SetCursorPos(cursorPos);

			if (ImGui::Button("Destroy"))
			{
				if (manager->has<TransformComponent>(selectedEntity))
				{
					auto transformSystem = manager->get<TransformSystem>();
					transformSystem->destroyRecursive(selectedEntity);
				}
				else
				{
					manager->destroy(selectedEntity);
					selectedEntity = {};
				}
				ImGui::End();
				return;
			}
		}

		for (auto& pair : entityInspectors)
		{
			if (manager->has(selectedEntity, pair.first))
				pair.second(selectedEntity);
		}
	}
	ImGui::End();

	if (!showEntityInspector)
		selectedEntity = {};
}

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showNewScene()
{
	if (!ImGui::IsPopupOpen("Create a new scene?"))
		ImGui::OpenPopup("Create a new scene?");

	if (ImGui::BeginPopupModal("Create a new scene?",
		nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("All unsaved scene changes will be lost.");
		ImGui::Spacing();

		if (ImGui::Button("OK", ImVec2(140.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup(); newScene = false;
			ResourceSystem::getInstance()->clearScene();
		}

		ImGui::SetItemDefaultFocus(); ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
			newScene = false;
		}
		ImGui::EndPopup();
	}
}

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::showExportScene()
{
	if (ImGui::Begin("Scene Exporter", &exportScene,
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Path", &scenePath);
		ImGui::BeginDisabled(scenePath.empty());
		if (ImGui::Button("Export .scene", ImVec2(-FLT_MIN, 0.0f)))
			ResourceSystem::getInstance()->storeScene(scenePath);
		ImGui::EndDisabled();
	}
	ImGui::End();
}

//--------------------------------------------------------------------------------------------------
void EditorRenderSystem::initialize()
{
	auto renderScale = 1.0f;
	auto settingsSystem = getManager()->tryGet<SettingsSystem>();
	if (settingsSystem)
		settingsSystem->getFloat("renderScale", renderScale);
	
	if (renderScale <= 0.5f)
		renderScaleType = 0;
	else if (renderScale <= 0.75f)
		renderScaleType = 1;
	else if (renderScale <= 1.0f)
		renderScaleType = 2;
	else if (renderScale <= 1.5f)
		renderScaleType = 3;
	else
		renderScaleType = 4;

	hierarchyEditor = new HierarchyEditor(this);
	resourceEditor = new ResourceEditor(this);
}
void EditorRenderSystem::terminate()
{
	delete (ResourceEditor*)resourceEditor;
	delete (HierarchyEditor*)hierarchyEditor;
}
void EditorRenderSystem::render()
{
	showMainMenuBar();

	if (demoWindow)
		ImGui::ShowDemoWindow(&demoWindow);
	if (aboutWindow)
		showAboutWindow();
	if (optionsWindow)
		showOptionsWindow();
	if (performanceStatistics)
		showPerformanceStatistics();
	if (memoryStatistics)
		showMemoryStatistics();
	if (newScene)
		showNewScene();
	if (exportScene)
		showExportScene();
	if (selectedEntity)
		showEntityInspector();

	((HierarchyEditor*)hierarchyEditor)->render();
	((ResourceEditor*)resourceEditor)->render();
}
#endif
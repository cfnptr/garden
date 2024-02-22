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

#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "garden/system/thread.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/render/fxaa.hpp"
#include "garden/system/render/deferred.hpp" // TODO: remove?
#include "garden/editor/system/render/resource.hpp"
#include "garden/editor/system/render/hierarchy.hpp"

#include "garden/graphics/glfw.hpp"
#include "garden/graphics/imgui-impl.hpp"
#include "garden/file.hpp"

#include "mpio/os.hpp"
#include "mpio/directory.hpp"

using namespace mpio;
using namespace garden;

//**********************************************************************************************************************
EditorRenderSystem::EditorRenderSystem(Manager* manager) : System(manager)
{
	manager->registerEventBefore("RenderEditor", "Present");
	manager->registerEvent("EditorBarFile");
	manager->registerEvent("EditorBarCreate");
	manager->registerEvent("EditorBarTool");

	SUBSCRIBE_TO_EVENT("RenderEditor", EditorRenderSystem::renderEditor);

	manager->createSystem<ResourceEditorSystem>(this);
	manager->createSystem<HierarchyEditorSystem>(this);
}
EditorRenderSystem::~EditorRenderSystem()
{
	auto manager = getManager();
	if (manager->isRunning())
	{
		manager->destroySystem<ResourceEditorSystem>();
		manager->destroySystem<HierarchyEditorSystem>();

		UNSUBSCRIBE_FROM_EVENT("RenderEditor", EditorRenderSystem::renderEditor);

		manager->unregisterEvent("RenderEditor");
		manager->unregisterEvent("EditorBarFile");
		manager->unregisterEvent("EditorBarCreate");
		manager->unregisterEvent("EditorBarTool");
	}
}

//**********************************************************************************************************************
void EditorRenderSystem::showMainMenuBar()
{
	auto manager = getManager();
	auto inputSystem = manager->get<InputSystem>();
	if (inputSystem->getCursorMode() == CursorMode::Locked)
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
		auto hasTransformSystem = manager->has<TransformSystem>();
		if (hasTransformSystem)
		{
			if (ImGui::MenuItem("New Scene"))
				newScene = true;
			if (ImGui::MenuItem("Export Scene"))
				exportScene = true;
		}

		const auto& subscribers = manager->getEventSubscribers("EditorBarFile");
		if (subscribers.empty())
		{
			if (!hasTransformSystem)
				ImGui::TextDisabled("Nothing here");
		}
		else
		{
			for (const auto& onBarFile : subscribers)
				onBarFile();
		}
			
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Create"))
	{
		const auto& subscribers = manager->getEventSubscribers("EditorBarCreate");
		if (subscribers.empty())
		{
			ImGui::TextDisabled("Nothing here");
		}
		else
		{
			for (const auto& onBarCreate : subscribers)
				onBarCreate();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Tools"))
	{
		const auto& subscribers = manager->getEventSubscribers("EditorBarTool");
		if (subscribers.empty())
		{
			ImGui::TextDisabled("Nothing here");
		}
		else
		{
			for (const auto& onBarTool : subscribers)
				onBarTool();
		}
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

//**********************************************************************************************************************
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

//**********************************************************************************************************************
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
		auto graphicsSystem = manager->get<GraphicsSystem>();

		if (ImGui::Checkbox("V-Sync", &graphicsSystem->useVsync))
		{
			auto settingsSystem = manager->tryGet<SettingsSystem>();
			if (settingsSystem)
				settingsSystem->setBool("useVsync", graphicsSystem->useVsync);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Triple Buffering", &graphicsSystem->useTripleBuffering);

		auto settingsSystem = manager->tryGet<SettingsSystem>();
		/* TODO: fix
		auto deferredSystem = manager->tryGet<DeferredRenderSystem>();
		auto fxaaSystem = manager->tryGet<FxaaRenderSystem>();

		if (fxaaSystem && deferredSystem)
		{
			ImGui::SameLine();
			if (ImGui::Checkbox("FXAA", &fxaaSystem->isEnabled))
			{
				deferredSystem->runSwapchainPass = !fxaaSystem->isEnabled;
				if (settingsSystem)
					settingsSystem->setBool("useFXAA", fxaaSystem->isEnabled);
			}
		}
		*/

		auto renderScale = 1.0f;
		if (settingsSystem)
			settingsSystem->getFloat("renderScale", renderScale);
	
		int renderScaleType;
		if (renderScale <= 0.5f) renderScaleType = 0;
		else if (renderScale <= 0.75f) renderScaleType = 1;
		else if (renderScale <= 1.0f) renderScaleType = 2;
		else if (renderScale <= 1.5f) renderScaleType = 3;
		else renderScaleType = 4;

		/* TODO: fix
		const auto renderScaleTypes = " 50%\0 75%\0 100%\0 150%\0 200%\0\0";
		if (deferredSystem && ImGui::Combo("Render Scale", renderScaleType, renderScaleTypes))
		{
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
			if (settingsSystem)
				settingsSystem->setFloat("renderScale", renderScale);
		}
		ImGui::Spacing();
		*/

		auto appInfoSystem = manager->get<AppInfoSystem>();
		auto cachePath = Directory::getAppDataPath(appInfoSystem->getAppDataName()) / "caches";
		int fileCount = 0; uint64 binarySize = 0;
		if (fs::exists(cachePath))
			getFileInfo(cachePath, fileCount, binarySize);
		auto sizeString = toBinarySizeString(binarySize);
		ImGui::Text("Application cache: %d files, %s", fileCount, sizeString.c_str());

		fileCount = 0; binarySize = 0;
		if (fs::exists(appInfoSystem->getCachesPath()))
			getFileInfo(appInfoSystem->getCachesPath(), fileCount, binarySize);
		sizeString = toBinarySizeString(binarySize);
		ImGui::Text("Project cache: %d files, %s", fileCount, sizeString.c_str());

		if (ImGui::Button("Clear application cache", ImVec2(-FLT_MIN, 0.0f)))
			fs::remove_all(cachePath);
		if (ImGui::Button("Clear project cache", ImVec2(-FLT_MIN, 0.0f)))
			fs::remove_all(appInfoSystem->getCachesPath());
	}
	ImGui::End();
}

//**********************************************************************************************************************
void EditorRenderSystem::showEntityInspector()
{
	auto showEntityInspector = true;
	if (ImGui::Begin("Entity Inspector", &showEntityInspector,
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto manager = getManager();
		ImGui::Text("ID: %d | Components: %d ", *selectedEntity, manager->getComponentCount(selectedEntity));

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

//**********************************************************************************************************************
void EditorRenderSystem::showNewScene()
{
	if (!ImGui::IsPopupOpen("Create a new scene?"))
		ImGui::OpenPopup("Create a new scene?");

	if (ImGui::BeginPopupModal("Create a new scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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

//**********************************************************************************************************************
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

//**********************************************************************************************************************
void EditorRenderSystem::renderEditor()
{
	showMainMenuBar();

	if (demoWindow)
		ImGui::ShowDemoWindow(&demoWindow);
	if (aboutWindow)
		showAboutWindow();
	if (optionsWindow)
		showOptionsWindow();
	if (newScene)
		showNewScene();
	if (exportScene)
		showExportScene();
	if (selectedEntity)
		showEntityInspector();
}
#endif
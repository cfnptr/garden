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

#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "garden/file.hpp"
#include "garden/json-serialize.hpp"
#include "garden/system/log.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/transform.hpp"
#include "garden/graphics/api.hpp"
#include "garden/graphics/glfw.hpp" // Note: Do not move it.
#include "garden/editor/system/render/gpu-resource.hpp"
#include "garden/profiler.hpp"

#include "mpio/directory.hpp"

using namespace garden;

//**********************************************************************************************************************
EditorRenderSystem::EditorRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->registerEvent("EditorPlayStart");
	manager->registerEvent("EditorPlayStop");
	manager->registerEvent("EditorBarFile");
	manager->registerEvent("EditorBarCreate");
	manager->registerEvent("EditorBarTool");
	manager->registerEvent("EditorBarToolPP");
	manager->registerEvent("EditorBar");
	manager->registerEvent("EditorSettings");

	ECSM_SUBSCRIBE_TO_EVENT("Init", EditorRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", EditorRenderSystem::deinit);
}
EditorRenderSystem::~EditorRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", EditorRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", EditorRenderSystem::deinit);

		auto manager = Manager::Instance::get();
		manager->unregisterEvent("EditorPlayStart");
		manager->unregisterEvent("EditorPlayStop");
		manager->unregisterEvent("EditorBarFile");
		manager->unregisterEvent("EditorBarCreate");
		manager->unregisterEvent("EditorBarTool");
		manager->unregisterEvent("EditorBarToolPP");
		manager->unregisterEvent("EditorBar");
		manager->unregisterEvent("EditorSettings");
	}

	unsetSingleton();
}

//**********************************************************************************************************************
static void renderSceneSelector(EditorRenderSystem* editorSystem, fs::path& exportScenePath)
{
	static const vector<string_view> extensions = { ".scene" };
	editorSystem->openFileSelector([&](const fs::path& selectedFile)
	{
		exportScenePath = selectedFile;
		exportScenePath.replace_extension();
		ResourceSystem::Instance::get()->loadScene(exportScenePath);
	},
	AppInfoSystem::Instance::get()->getResourcesPath() / "scenes", extensions);
}

void EditorRenderSystem::showMainMenuBar()
{
	if (InputSystem::Instance::get()->getCursorMode() == CursorMode::Locked)
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
			glfwSetWindowShouldClose((GLFWwindow*)GraphicsAPI::get()->window, GLFW_TRUE);
		ImGui::EndMenu();
	}

	auto manager = Manager::Instance::get();
	if (ImGui::BeginMenu("File"))
	{
		auto hasTransformSystem = TransformSystem::Instance::has();
		if (hasTransformSystem)
		{
			if (ImGui::MenuItem("New Scene"))
				newScene = true;
			if (ImGui::MenuItem("Export Scene"))
				exportScene = true;
			if (ImGui::MenuItem("Import Scene"))
				renderSceneSelector(this, exportsScenePath);
		}

		const auto& event = manager->getEvent("EditorBarFile");
		if (event.hasSubscribers())
		{
			event.run();
		}
		else
		{
			if (!hasTransformSystem)
				ImGui::TextDisabled("Nothing here");
		}
			
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Create"))
	{
		const auto& event = manager->getEvent("EditorBarCreate");
		if (event.hasSubscribers())
			event.run();
		else
			ImGui::TextDisabled("Nothing here");
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Tools"))
	{
		const auto& toolEvent = manager->getEvent("EditorBarTool");
		if (toolEvent.hasSubscribers())
		{
			toolEvent.run();
			
			const auto& ppEvent = manager->getEvent("EditorBarToolPP");
			if (ppEvent.hasSubscribers() && ImGui::BeginMenu("Post-Processing"))
			{
				ppEvent.run();
				ImGui::EndMenu();
			}
		}
		else
		{
			ImGui::TextDisabled("Nothing here");
		}
		ImGui::EndMenu();
	}

	manager->runEvent("EditorBar");

	auto playText = playing ? "Stop []" : "Play |>";
	auto textSize = ImGui::CalcTextSize(playText);
	ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - (textSize.x * 0.5f + 12.0f));

	if (playing)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_Header]);
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered]);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
	}

	if (ImGui::Button(playText))
		setPlaying(!playing);
	ImGui::PopStyleColor(3);
	
	auto stats = "[E: " + to_string(manager->getEntities().getCount());

	auto threadSystem = ThreadSystem::Instance::tryGet();
	if (threadSystem)
	{
		auto& threadPool = threadSystem->getBackgroundPool();
		stats += " | T: " + to_string(threadPool.getPendingTaskCount());
	}

	auto inputSystem = InputSystem::Instance::get();
	auto fps = 1.0 / (inputSystem->getDeltaTime() / inputSystem->timeMultiplier);
	stats += " | FPS: " + to_string((int32)((lastFps + fps) * 0.5));
	lastFps = fps;

	stats += "]";

	textSize = ImGui::CalcTextSize(stats.c_str());
	ImGui::SameLine(ImGui::GetWindowWidth() - (textSize.x + 16.0f));
	ImGui::Text("%s", stats.c_str());

	if (ImGui::BeginItemTooltip())
	{
		ImGui::Text("E = Entities, T = Tasks, FPS = Frames Per Second");
		ImGui::EndTooltip();
	}

	ImGui::EndMainMenuBar();
}

//**********************************************************************************************************************
void EditorRenderSystem::showAboutWindow()
{
	if (ImGui::Begin("About", &aboutWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto engineVersion = string(GARDEN_VERSION_STRING);
		if (GARDEN_VERSION_MAJOR == 0 && GARDEN_VERSION_MINOR == 0)
			engineVersion += " (Alpha)";
		else if (GARDEN_VERSION_MAJOR == 0)
			engineVersion += " (Beta)";

		ImGui::SeparatorText(GARDEN_NAME_STRING " Engine");
		ImGui::Text("Creator: Nikita Fediuchin");
		ImGui::Text("Version: %s", engineVersion.c_str());

		auto appInfoSystem = AppInfoSystem::Instance::tryGet();
		if (appInfoSystem)
		{
			auto appVersion = appInfoSystem->getVersion().toString3();
			ImGui::SeparatorText("Application");
			ImGui::Text("Name: %s", appInfoSystem->getName().c_str());
			ImGui::Text("Creator: %s", appInfoSystem->getCreator().c_str());
			ImGui::Text("Version: %s", appVersion.c_str());
			#ifdef NDEBUG
				#if GARDEN_DEBUG
				ImGui::Text("Build: Release (Debugging)");
				#else
				ImGui::Text("Build: Release");
				#endif
			#else
			ImGui::Text("Build: Debug");
			#endif
			ImGui::Text("Target OS: " GARDEN_OS_NAME " (" GARDEN_CPU_ARCH ")");
		}
	}
	ImGui::End();
}

//**********************************************************************************************************************
static void getFileInfo(const fs::path& path, int& fileCount, uint64& binarySize)
{
	auto iterator = fs::directory_iterator(path);
	for (const auto& entry : iterator)
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
	if (ImGui::Begin("Options", &optionsWindow))
	{
		auto manager = Manager::Instance::get();
		auto graphicsSystem = GraphicsSystem::Instance::get();
		auto settingsSystem = SettingsSystem::Instance::tryGet();

		if (ImGui::Checkbox("V-Sync", &graphicsSystem->useVsync))
		{
			if (settingsSystem)
				settingsSystem->setBool("useVsync", graphicsSystem->useVsync);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Triple Buffering", &graphicsSystem->useTripleBuffering);

		auto renderScale = 1.0f;
		if (settingsSystem)
			settingsSystem->getFloat("renderScale", renderScale);
	
		int renderScaleType;
		if (renderScale <= 0.5f) renderScaleType = 0;
		else if (renderScale <= 0.75f) renderScaleType = 1;
		else if (renderScale <= 1.0f) renderScaleType = 2;
		else if (renderScale <= 1.5f) renderScaleType = 3;
		else renderScaleType = 4;

		constexpr const char* scaleNames[5] = { " 50%", " 75%", " 100%", " 150%", " 200%" };
		if (ImGui::Combo("Render Scale", &renderScaleType, scaleNames, 5))
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
			
			graphicsSystem->setRenderScale(renderScale);
			if (settingsSystem)
				settingsSystem->setFloat("renderScale", renderScale);
		}

		auto frameRate = (int)graphicsSystem->maxFPS;
		if (ImGui::DragInt("Max FPS", &frameRate, 1, 1, UINT16_MAX))
		{
			graphicsSystem->maxFPS = (uint16)frameRate;
			if (settingsSystem)
				settingsSystem->setInt("maxFPS", frameRate);
		}
		ImGui::Spacing();

		auto appInfoSystem = AppInfoSystem::Instance::tryGet();
		if (appInfoSystem && ImGui::CollapsingHeader("Storage"))
		{
			ImGui::Indent();
			auto appDataPath = mpio::Directory::getAppDataPath(appInfoSystem->getAppDataName());
			auto cachePath = appDataPath / "cache";
			int fileCount = 0; uint64 binarySize = 0;

			if (fs::exists(cachePath))
				getFileInfo(cachePath, fileCount, binarySize);
			auto sizeString = toBinarySizeString(binarySize);
			ImGui::Text("Application cache: %d files, %s", fileCount, sizeString.c_str());

			fileCount = 0; binarySize = 0;
			if (fs::exists(appInfoSystem->getCachePath()))
				getFileInfo(appInfoSystem->getCachePath(), fileCount, binarySize);
			sizeString = toBinarySizeString(binarySize);
			ImGui::Text("Project cache: %d files, %s", fileCount, sizeString.c_str());

			if (ImGui::Button("Clear application cache", ImVec2(-FLT_MIN, 0.0f)))
				fs::remove_all(cachePath);
			if (ImGui::Button("Clear project cache", ImVec2(-FLT_MIN, 0.0f)))
				fs::remove_all(appInfoSystem->getCachePath());
			if (ImGui::Button("Delete settings file", ImVec2(-FLT_MIN, 0.0f)))
				fs::remove(appDataPath / "settings.txt");
			ImGui::Unindent();
			ImGui::Spacing();
		}

		manager->runEvent("EditorSettings");
	}
	ImGui::End();
}

//**********************************************************************************************************************
namespace garden
{
	struct ComponentEntry final
	{
		using Nodes = unordered_map<string, ComponentEntry>;
		
		Nodes nodes;
		type_index componentType;
		ComponentEntry(type_index componentType) : componentType(componentType) { }
	};
}

// TODO: replace with stack based recursion.
static void renderWordNode(const ComponentEntry::Nodes& nodes, ID<Entity> selectedEntity)
{
	for (const auto& pair : nodes)
	{
		if (pair.second.nodes.empty())
		{
			if (ImGui::MenuItem(pair.first.c_str()))
				Manager::Instance::get()->add(selectedEntity, pair.second.componentType);
		}
		else
		{
			if (ImGui::BeginMenu(pair.first.c_str()))
			{
				renderWordNode(pair.second.nodes, selectedEntity);
				ImGui::EndMenu();
			}
		}
	}
}
static void renderAddComponent(const EditorRenderSystem::EntityInspectors& entityInspectors, 
	ID<Entity> selectedEntity, uint32& itemCount)
{
	auto manager = Manager::Instance::get();
	const auto& componentTypes = manager->getComponentTypes();
	static ComponentEntry::Nodes wordNodes;

	for (const auto& pair : componentTypes)
	{
		if (pair.second->getComponentName().empty())
			continue;
		itemCount++;

		if (entityInspectors.find(pair.first) == entityInspectors.end() ||
			manager->has(selectedEntity, pair.first))
		{
			continue;
		}

		auto currentNode = &wordNodes;
		auto componentName = pair.second->getComponentName();
		auto lastSpace = componentName.length();
		auto isRunning = true;

		while (isRunning)
		{
			auto currentSpace = componentName.rfind(' ', lastSpace - 1);

			psize length = 0;
			if (currentSpace == string::npos)
			{
				currentSpace = (psize)-1;
				length = lastSpace;
				isRunning = false;
			}
			else if (currentSpace == 0)
			{
				currentSpace = 0;
				length = lastSpace - 1;
				isRunning = false;
			}
			else
			{
				length = lastSpace - (currentSpace + 1);
				if (length == 0)
				{
					lastSpace = currentSpace;
					continue;
				}
			}

			auto word = string(componentName, (currentSpace + 1), length);

			auto searchResult = currentNode->find(word);
			if (searchResult == currentNode->end())
			{
				ComponentEntry entry(pair.first);
				auto emplaceResult = currentNode->emplace(std::move(word), std::move(entry));
				GARDEN_ASSERT_MSG(emplaceResult.second, "Detected memory corruption");
				currentNode = &emplaceResult.first->second.nodes;
			}
			else
			{
				currentNode = &searchResult->second.nodes;
			}

			lastSpace = currentSpace;
		}
	}

	renderWordNode(wordNodes, selectedEntity);
	wordNodes.clear();
}

static bool renderInspectorWindowPopup(const EditorRenderSystem::EntityInspectors& 
	entityInspectors, ID<Entity>& selectedEntity)
{
	if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		auto manager = Manager::Instance::get();
		const auto& componentTypes = manager->getComponentTypes();

		if (ImGui::BeginMenu("Add Component", !componentTypes.empty()))
		{
			uint32 itemCount = 0;
			renderAddComponent(entityInspectors, selectedEntity, itemCount);

			if (ImGui::BeginMenu("Tags"))
			{
				for (const auto& pair : componentTypes)
				{
					if (pair.second->getComponentName().empty())
						continue;

					if (entityInspectors.find(pair.first) != entityInspectors.end() ||
						manager->has(selectedEntity, pair.first))
					{
						continue;
					}

					if (ImGui::MenuItem(string(pair.second->getComponentName()).c_str()))
						manager->add(selectedEntity, pair.first);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Others", itemCount != componentTypes.size()))
			{
				for (const auto& pair : componentTypes)
				{
					if (!pair.second->getComponentName().empty() ||
						manager->has(selectedEntity, pair.first))
					{
						continue;
					}
					auto componentName = typeToString(pair.first);
					if (ImGui::MenuItem(componentName.c_str()))
						manager->add(selectedEntity, pair.first);
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		auto transformView = manager->tryGet<TransformComponent>(selectedEntity);
		if (ImGui::MenuItem("Select Parent", nullptr, false, transformView && transformView->getParent()))
		{
			selectedEntity = transformView->getParent();
			ImGui::EndPopup();
			return false;
		}
		if (ImGui::MenuItem("Destroy Entity", nullptr, false, !manager->has<DoNotDestroyComponent>(selectedEntity)))
		{
			TransformSystem::Instance::get()->destroyRecursive(selectedEntity);
			selectedEntity = {};
			ImGui::EndPopup();
			return false;
		}
		ImGui::EndPopup();
	}

	return true;
}

//**********************************************************************************************************************
static bool renderInspectorComponentPopup(ID<Entity>& selectedEntity,
	System* system, type_index componentType, string_view componentName)
{
	if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Remove Component"))
		{
			auto manager = Manager::Instance::get();
			auto selected = selectedEntity; // Note: Do not optimize, required for transforms.
			manager->remove(selectedEntity, componentType);
			if (!manager->hasComponents(selected))
				manager->destroy(selected);
			ImGui::EndPopup();
			return false;
		}

		if (ImGui::MenuItem("Reset Component"))
			Manager::Instance::get()->reset(selectedEntity, componentType);

		if (ImGui::MenuItem("Copy Component Name"))
			ImGui::SetClipboardText(string(componentName).c_str());

		auto serializableSystem = dynamic_cast<ISerializable*>(system);
		if (ImGui::MenuItem("Copy Component Data", nullptr, false, serializableSystem))
		{
			auto manager = Manager::Instance::get();
			JsonSerializer jsonSerializer;
			serializableSystem->preSerialize(jsonSerializer);
			auto componentView = manager->get(selectedEntity, componentType);
			serializableSystem->serialize(jsonSerializer, componentView);
			serializableSystem->postSerialize(jsonSerializer);
			ImGui::SetClipboardText(jsonSerializer.toString().c_str());
		}
		if (ImGui::MenuItem("Paste Component Data", nullptr, false, serializableSystem))
		{
			auto manager = Manager::Instance::get();
			auto stagingEntity = manager->createEntity(); // TODO: maybe add resetComponent function instead?
			manager->add(stagingEntity, componentType);
			try
			{
				JsonDeserializer jsonDeserializer = JsonDeserializer(string_view(ImGui::GetClipboardText()));
				serializableSystem->preDeserialize(jsonDeserializer);
				auto componentView = manager->get(stagingEntity, componentType);
				serializableSystem->deserialize(jsonDeserializer, componentView);
				serializableSystem->postDeserialize(jsonDeserializer);
				manager->copy(stagingEntity, selectedEntity, componentType);
			}
			catch (exception& e)
			{
				GARDEN_LOG_ERROR("Failed to deserialize component data on paste. (error: " + string(e.what()) + ")");
			}
			manager->destroy(stagingEntity);
		}

		ImGui::EndPopup();
	}

	return true;
}

//**********************************************************************************************************************
void EditorRenderSystem::showEntityInspector()
{
	ImGui::SetNextWindowSize(ImVec2(384.0f, 256.0f), ImGuiCond_FirstUseEver);

	auto showEntityInspector = true;
	if (ImGui::Begin("Entity Inspector", &showEntityInspector, ImGuiWindowFlags_NoFocusOnAppearing))
	{
		auto manager = Manager::Instance::get();
		auto entityView = manager->getEntities().get(selectedEntity);
		auto components = entityView->getComponents();
		auto componentCount = entityView->getComponentCount();

		if (ImGui::BeginItemTooltip())
		{
			ImGui::Text("Runtime ID: %lu, Components: %lu / %lu", (unsigned long)*selectedEntity, 
				(unsigned long)componentCount, (unsigned long)entityView->getComponentCapacity());
			ImGui::EndTooltip();
		}
		
		if (!renderInspectorWindowPopup(entityInspectors, selectedEntity))
		{
			ImGui::End();
			return;
		}

		for (uint32 i = 0; i < componentCount; i++)
		{
			auto system = components[i].system;
			auto result = entityInspectors.find(system->getComponentType());
			if (result == entityInspectors.end())
				continue;
			onComponents.emplace(result->second.priority, make_pair(system, result->second.onComponent));
		}
		for (const auto& pair : onComponents)
		{
			auto system = pair.second.first;
			auto componentName = system->getComponentName().empty() ?
				typeToString(system->getComponentType()) : string(system->getComponentName());
			ImGui::PushID(componentName.c_str());
			auto isOpened = ImGui::CollapsingHeader(componentName.c_str());
				
			if (!renderInspectorComponentPopup(selectedEntity, 
				system, system->getComponentType(), componentName))
			{
				ImGui::PopID();
				continue;
			}

			ImGui::Indent();
			pair.second.second(selectedEntity, isOpened);
			ImGui::Unindent();

			if (isOpened)
				ImGui::Spacing();
			ImGui::PopID();
		}
		onComponents.clear();

		for (uint32 i = 0; i < componentCount; i++)
		{
			auto system = components[i].system;
			if (entityInspectors.find(system->getComponentType()) == entityInspectors.end())
			{
				auto componentType = system->getComponentType();
				auto componentName = system->getComponentName().empty() ? 
					typeToString(componentType) : string(system->getComponentName());
				ImGui::CollapsingHeader(componentName.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet);
				
				if (!renderInspectorComponentPopup(selectedEntity, system, componentType, componentName))
					continue;
			}
		}
	}
	ImGui::End();

	if (InputSystem::Instance::get()->isKeyboardPressed(KeyboardButton::Delete) &&
		!Manager::Instance::get()->has<DoNotDestroyComponent>(selectedEntity))
	{
		TransformSystem::Instance::get()->destroyRecursive(selectedEntity);
		selectedEntity = {};
	}

	if (!showEntityInspector)
		selectedEntity = {};
}

//**********************************************************************************************************************
void EditorRenderSystem::showNewScene()
{
	if (!ImGui::IsPopupOpen("Create a new scene?"))
		ImGui::OpenPopup("Create a new scene?");

	auto size = ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f);
	ImGui::SetNextWindowPos(size, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Create a new scene?", nullptr,
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("All unsaved scene changes will be lost.");
		ImGui::Spacing();

		if (ImGui::Button("OK", ImVec2(140.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup(); newScene = false;
			ResourceSystem::Instance::get()->clearScene();
			exportsScenePath = "unnamed";
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

void EditorRenderSystem::showExportScene()
{
	if (ImGui::Begin("Scene Exporter", &exportScene,
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto pathString = exportsScenePath.generic_string();
		if (ImGui::InputText("Path", &pathString))
			exportsScenePath = pathString;

		ImGui::BeginDisabled(exportsScenePath.empty());
		if (ImGui::Button("Export full .scene", ImVec2(-FLT_MIN, 0.0f)))
			ResourceSystem::Instance::get()->storeScene(exportsScenePath);
		ImGui::EndDisabled();

		auto manager = Manager::Instance::get();
		ImGui::BeginDisabled(!selectedEntity || !manager->has<TransformComponent>(selectedEntity));
		string exportSelectedTest = "Export selected .scene";
		if (selectedEntity)
		{
			auto transformView = manager->tryGet<TransformComponent>(selectedEntity);
			auto debugName = transformView->debugName.empty() ?
				"Entity " + to_string(*selectedEntity) : transformView->debugName;
			exportSelectedTest += " (" + debugName + ")";
		}
		if (ImGui::Button(exportSelectedTest.c_str(), ImVec2(-FLT_MIN, 0.0f)))
			ResourceSystem::Instance::get()->storeScene(exportsScenePath, selectedEntity);
		ImGui::EndDisabled();
	}
	ImGui::End();
}

//**********************************************************************************************************************
static void openExplorer(const fs::path& path) // TODO: make this function public, move it to the mpio library
{
	#if GARDEN_OS_WINDOWS
	std::system(("start " + path.generic_string()).c_str());
	#elif GARDEN_OS_MACOS
	std::system(("open " + path.generic_string()).c_str());
	#elif GARDEN_OS_LINUX
	std::system(("xdg-open " + path.generic_string()).c_str());
	#endif
}
static bool isHasDirectories(const fs::path& path)
{
	auto dirIterator = fs::directory_iterator(path);
	for (const auto& entry : dirIterator)
	{
		if (entry.is_directory())
			return true;
	}
	return false;
}
static void updateDirectoryClick(const string& filename, const fs::directory_entry& entry, fs::path& selectedEntry)
{
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		selectedEntry = entry.path();

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Copy Name"))
			ImGui::SetClipboardText(filename.c_str());
		if (ImGui::MenuItem("Copy Path"))
			ImGui::SetClipboardText(entry.path().generic_string().c_str());
		if (ImGui::MenuItem("Open Explorer"))
			openExplorer(entry.path());
		ImGui::EndPopup();
	}
}
static void renderDirectory(const fs::path& path, fs::path& selectedEntry)
{
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);
	auto dirIterator = fs::directory_iterator(path);
	for (const auto& entry : dirIterator)
	{
		if (!entry.is_directory())
			continue;

		auto hasDirectories = isHasDirectories(entry.path());
		auto flags = (int)ImGuiTreeNodeFlags_OpenOnArrow;
		if (!hasDirectories)
			flags |= (int)ImGuiTreeNodeFlags_Leaf;
		if (selectedEntry == entry.path())
			flags |= (int)ImGuiTreeNodeFlags_Selected;

		auto filename = entry.path().filename().generic_string();
		ImGui::PushID(entry.path().generic_string().c_str());
		if (ImGui::TreeNodeEx(filename.c_str(), flags))
		{
			updateDirectoryClick(filename, entry, selectedEntry);
			if (hasDirectories)
				renderDirectory(entry.path(), selectedEntry); // TODO: use stack instead of recursion here!
			ImGui::TreePop();
		}
		else
		{
			updateDirectoryClick(filename, entry, selectedEntry);
		}
		ImGui::PopID();
	}
	ImGui::PopStyleColor();
}

//**********************************************************************************************************************
static time_t to_time_t(fs::file_time_type tp) noexcept
{
	// Dirty hack to convert last_write_time to the time_t. Because C++ standard is a big pile of garbage.
    auto sctp = chrono::time_point_cast<chrono::system_clock::duration>(
		tp - fs::file_time_type::clock::now() + chrono::system_clock::now());
    return chrono::system_clock::to_time_t(sctp);
}

void EditorRenderSystem::showFileSelector()
{
	if (!ImGui::IsPopupOpen("File Selector"))
		ImGui::OpenPopup("File Selector");

	auto size = ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f);
	ImGui::SetNextWindowPos(size, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(640.0f, 320.0f), ImGuiCond_FirstUseEver);

	if (ImGui::BeginPopupModal("File Selector", nullptr, ImGuiWindowFlags_NoMove))
	{
		ImGui::Text("%s", selectedEntry.generic_string().c_str());

		ImGui::BeginChild("##itemList", ImVec2(256.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)),
			ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
		
		if (fs::exists(fileSelectDirectory) && fs::is_directory(fileSelectDirectory))
		{
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				selectedEntry = fileSelectDirectory;
			renderDirectory(fileSelectDirectory, selectedEntry);
		}

		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##itemView", ImVec2(0.0f, 
			-(ImGui::GetFrameHeightWithSpacing() + 4.0f)), ImGuiChildFlags_Borders);
		auto foundAny = false;

		if (ImGui::BeginTable("##fileTable", 3, ImGuiTableFlags_BordersInner | 
			ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Button]);
			ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImGui::GetStyle().Colors[ImGuiCol_TableRowBg]);
			ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_IndentDisable);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_IndentDisable);
			ImGui::TableSetupColumn("Date modified", ImGuiTableColumnFlags_IndentDisable);
			ImGui::TableHeadersRow();

			if (fs::exists(selectedEntry) && fs::is_directory(selectedEntry))
			{
				auto dirIterator = fs::directory_iterator(selectedEntry);
				for (const auto& entry : dirIterator)
				{
					if (entry.is_directory())
						continue;

					auto extensionNotFound = true;
					auto extension = entry.path().extension().generic_string();
					for (auto fileExtension : fileExtensions)
					{
						if (extension == fileExtension)
						{
							extensionNotFound = false;
							break;
						}
					}

					if (extensionNotFound)
						continue;

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					auto stringValue = entry.path().filename().generic_string();
					auto flags = (int)(ImGuiTreeNodeFlags_Leaf | 
						ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAllColumns);
					if (selectedFile == entry.path())
						flags |= (int)ImGuiTreeNodeFlags_Selected;
					ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
					ImGui::TreeNodeEx(stringValue.c_str(), flags);

					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
						selectedFile = entry.path();
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Copy Name"))
							ImGui::SetClipboardText(stringValue.c_str());
						if (ImGui::MenuItem("Copy Path"))
							ImGui::SetClipboardText(entry.path().generic_string().c_str());
						if (ImGui::MenuItem("Open Explorer"))
							openExplorer(entry.path());
						ImGui::EndPopup();
					}

					ImGui::TableNextColumn();
					stringValue = toBinarySizeString(entry.file_size());
					ImGui::Text("%s", stringValue.c_str());

					ImGui::TableNextColumn();
					auto lastWriteTime = to_time_t(entry.last_write_time());
					auto data = localtime(&lastWriteTime);
					ImGui::Text("%d-%02d-%02d %02d:%02d", data->tm_year + 1900, 
						data->tm_mon + 1, data->tm_mday, data->tm_hour, data->tm_min);
					foundAny = true;
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleColor(2);
		}

		if (!foundAny)
		{
			const auto text = "No suitable files.";
			auto textSize = ImGui::CalcTextSize(text);
			auto childSize = ImGui::GetContentRegionAvail();
			auto textPosX = (childSize.x - textSize.x) * 0.5f;
			auto textPosY = (childSize.y - textSize.y) * 0.5f;
			ImGui::SetCursorPos(ImVec2(textPosX, textPosY));
			ImGui::TextDisabled(text);
		}

		ImGui::EndChild();
		ImGui::Spacing();

		auto filePath = selectedFile.generic_string();
		ImGui::SetNextItemWidth(std::max(ImGui::GetWindowWidth() - 190.0f, 128.0f));
		ImGui::InputText("File", &filePath, ImGuiInputTextFlags_ReadOnly);

		ImGui::SameLine();
		ImGui::BeginDisabled(!fs::exists(selectedFile) || fs::is_directory(selectedFile));
		if (ImGui::Button("Select"))
		{
			auto dir = fileSelectDirectory.generic_string() + "/";
			auto path = selectedFile.generic_string();
			auto it = path.find(dir);
			if (it != std::string::npos)
				path.erase(it, dir.length());
			onFileSelect(path);
			fileSelectDirectory = "";
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			fileSelectDirectory = "";

		ImGui::EndPopup();
	}
}

//**********************************************************************************************************************
void EditorRenderSystem::init()
{
	ECSM_SUBSCRIBE_TO_EVENT("PreUiRender", EditorRenderSystem::preUiRender);
}
void EditorRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("PreUiRender", EditorRenderSystem::preUiRender);
}

void EditorRenderSystem::preUiRender()
{
	SET_CPU_ZONE_SCOPED("Pre UI Render");

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
	if (!fileSelectDirectory.empty())
		showFileSelector();
}

void EditorRenderSystem::setPlaying(bool isPlaying)
{
	if (this->playing == isPlaying)
		return;

	if (playing)
	{
		Manager::Instance::get()->runEvent("EditorPlayStop");
		this->playing = false;
	}
	else
	{
		Manager::Instance::get()->runEvent("EditorPlayStart");
		this->playing = true;
	}
}

//**********************************************************************************************************************
void EditorRenderSystem::openFileSelector(const OnFileSelect& onSelect, 
	const fs::path& directory, const vector<string_view>& extensions)
{
	fileSelectDirectory = selectedEntry = directory.empty() ?
		AppInfoSystem::Instance::get()->getResourcesPath() : directory;
	fileExtensions = extensions;
	onFileSelect = onSelect;
}

void EditorRenderSystem::drawFileSelector(const char* name, fs::path& path, ID<Entity> entity,
	type_index componentType, const fs::path& directory, const vector<string_view>& extensions)
{
	GARDEN_ASSERT(name);
	GARDEN_ASSERT(entity);

	auto pathString = path.generic_string();
	if (ImGui::InputText(name, &pathString, ImGuiInputTextFlags_ReadOnly))
		path = pathString;

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Select File"))
		{
			openFileSelector([&](const fs::path& selectedFile)
			{
				if (EditorRenderSystem::Instance::get()->selectedEntity != entity ||
					!Manager::Instance::get()->has(entity, componentType))
				{
					return;
				}

				path = selectedFile;
				path.replace_extension();
			},
			AppInfoSystem::Instance::get()->getResourcesPath() / directory, extensions);
		}

		if (ImGui::MenuItem("Reset Default"))
			path = "";
		ImGui::EndPopup();
	}
}

//**********************************************************************************************************************
void EditorRenderSystem::drawImageSelector(const char* name, fs::path& path, Ref<Image>& image, 
	Ref<DescriptorSet>& descriptorSet, ID<Entity> entity, type_index componentType, ImageLoadFlags loadFlags)
{
	GARDEN_ASSERT(name);
	GARDEN_ASSERT(entity);

	auto pathString = path.generic_string();
	if (ImGui::InputText(name, &pathString, ImGuiInputTextFlags_ReadOnly))
		path = pathString;

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Select File"))
		{
			openFileSelector([&](const fs::path& selectedFile)
			{
				if (EditorRenderSystem::Instance::get()->selectedEntity != entity ||
					!Manager::Instance::get()->has(entity, componentType))
				{
					return;
				}
			
				auto resourceSystem = ResourceSystem::Instance::get();
				resourceSystem->destroyShared(image);
				resourceSystem->destroyShared(descriptorSet);

				path = selectedFile;
				path.replace_extension();

				image = resourceSystem->loadImage(path, Image::Usage::Sampled | 
					Image::Usage::TransferDst, 1, Image::Strategy::Default, loadFlags);
				descriptorSet = {};
			},
			AppInfoSystem::Instance::get()->getResourcesPath() / "images", ResourceSystem::imageFileExts);
		}

		auto gpuResourceSystem = Manager::Instance::get()->tryGet<GpuResourceEditorSystem>();
		if (ImGui::MenuItem("Show Resource", nullptr, false, gpuResourceSystem && image))
			gpuResourceSystem->openTab(ID<Image>(image));
		if (ImGui::MenuItem("Reset Default"))
		{
			auto resourceSystem = ResourceSystem::Instance::get();
			resourceSystem->destroyShared(image);
			resourceSystem->destroyShared(descriptorSet);
			path = ""; image = {}; descriptorSet = {};
		}
		ImGui::EndPopup();
	}
}

//**********************************************************************************************************************
static void drawResource(const Resource* resource, const char* label,
	ID<Resource> instance, GpuResourceEditorSystem::TabType tabType)
{
	string bufferViewName;
	if (resource)
	{
		bufferViewName = resource->getDebugName().empty() ?
			to_string(*instance) : resource->getDebugName();
	}

	ImGui::InputText(label, &bufferViewName, ImGuiInputTextFlags_ReadOnly);

	if (ImGui::BeginPopupContextItem())
	{
		auto gpuResourceSystem = Manager::Instance::get()->tryGet<GpuResourceEditorSystem>();
		if (ImGui::MenuItem("Show Resource", nullptr, false, gpuResourceSystem && resource))
			gpuResourceSystem->openTab(instance, tabType);
		ImGui::EndPopup();
	}
}
void EditorRenderSystem::drawResource(ID<Buffer> buffer, const char* label)
{
	auto bufferView = buffer ? GraphicsAPI::get()->bufferPool.get(buffer) : View<Buffer>();
	::drawResource(*bufferView, label, ID<Resource>(buffer), GpuResourceEditorSystem::TabType::Buffers);
}
void EditorRenderSystem::drawResource(ID<Image> image, const char* label)
{
	auto imageView = image ? GraphicsAPI::get()->imagePool.get(image) : View<Image>();
	::drawResource(*imageView, label, ID<Resource>(image), GpuResourceEditorSystem::TabType::Images);
}
void EditorRenderSystem::drawResource(ID<ImageView> imageView, const char* label)
{
	auto imageViewView = imageView ? GraphicsAPI::get()->imageViewPool.get(imageView) : View<ImageView>();
	::drawResource(*imageViewView, label, ID<Resource>(imageView), GpuResourceEditorSystem::TabType::ImageViews);
}
void EditorRenderSystem::drawResource(ID<Framebuffer> framebuffer, const char* label)
{
	auto framebufferView = framebuffer ? 
		GraphicsAPI::get()->framebufferPool.get(framebuffer) : View<Framebuffer>();
	::drawResource(*framebufferView, label, ID<Resource>(framebuffer), 
		GpuResourceEditorSystem::TabType::Framebuffers);
}
void EditorRenderSystem::drawResource(ID<Sampler> sampler, const char* label)
{
	auto samplerView = sampler ? GraphicsAPI::get()->samplerPool.get(sampler) : View<Sampler>();
	::drawResource(*samplerView, label, ID<Resource>(sampler), GpuResourceEditorSystem::TabType::Samplers);
}
void EditorRenderSystem::drawResource(ID<Blas> blas, const char* label)
{
	auto blasView = blas ? GraphicsAPI::get()->blasPool.get(blas) : View<Blas>();
	::drawResource(*blasView, label, ID<Resource>(blas), GpuResourceEditorSystem::TabType::Blases);
}
void EditorRenderSystem::drawResource(ID<Tlas> tlas, const char* label)
{
	auto tlasView = tlas ? GraphicsAPI::get()->tlasPool.get(tlas) : View<Tlas>();
	::drawResource(*tlasView, label, ID<Resource>(tlas), GpuResourceEditorSystem::TabType::Tlases);
}
void EditorRenderSystem::drawResource(ID<DescriptorSet> descriptorSet, const char* label)
{
	auto descriptorSetView = descriptorSet ?
		GraphicsAPI::get()->descriptorSetPool.get(descriptorSet) : View<DescriptorSet>();
	::drawResource(*descriptorSetView, label, ID<Resource>(descriptorSet),
		GpuResourceEditorSystem::TabType::DescriptorSets);
}
void EditorRenderSystem::drawResource(ID<GraphicsPipeline> graphicsPipeline, const char* label)
{
	auto graphicsPipelineView = graphicsPipeline ? 
		GraphicsAPI::get()->graphicsPipelinePool.get(graphicsPipeline) : View<GraphicsPipeline>();
	::drawResource(*graphicsPipelineView, label, ID<Resource>(graphicsPipeline),
		GpuResourceEditorSystem::TabType::GraphicsPipelines);
}
void EditorRenderSystem::drawResource(ID<ComputePipeline> computePipeline, const char* label)
{
	auto computePipelineView = computePipeline ?
		GraphicsAPI::get()->computePipelinePool.get(computePipeline) : View<ComputePipeline>();
	::drawResource(*computePipelineView, label, ID<Resource>(computePipeline),
		GpuResourceEditorSystem::TabType::ComputePipelines);
}
void EditorRenderSystem::drawResource(ID<RayTracingPipeline> rayTracingPipeline, const char* label)
{
	auto rayTracingPipelineView = rayTracingPipeline ?
		GraphicsAPI::get()->rayTracingPipelinePool.get(rayTracingPipeline) : View<RayTracingPipeline>();
	::drawResource(*rayTracingPipelineView, label, ID<Resource>(rayTracingPipeline),
		GpuResourceEditorSystem::TabType::RayTracingPipelines);
}
#endif
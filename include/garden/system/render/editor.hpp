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

/***********************************************************************************************************************
 * @file
 * @brief Editor GUI render functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/imgui.hpp"
#include "ecsm.hpp"
#include "math/aabb.hpp"

namespace garden
{

using namespace ecsm;
using namespace garden::graphics;

/***********************************************************************************************************************
 * @brief Editor GUI rendering system.
 * 
 * @details
 * Editor is the suite of tools and interfaces provided by a game engine for creating and editing digital content, 
 * including video games and interactive media. It encompasses tools for building scenes, managing digital assets like 
 * models and textures, scripting behavior, testing the game within the editor, and designing user interfaces.
 * 
 * Registers events: EditorRender, EditorBarFile, EditorBarCreate, EditorBarTool.
 */
class EditorRenderSystem final : public System
{
public:
	using OnComponent = std::function<void(ID<Entity> entity, bool isOpened)>;
private:
	map<type_index, OnComponent> entityInspectors;
	string scenePath = "unnamed";
	fs::path fileSelectDirectory;
	std::function<void(const fs::path)> onFileSelect;
	bool demoWindow = false;
	bool aboutWindow = false;
	bool optionsWindow = false;
	bool newScene = false;
	bool exportScene = false;

	static EditorRenderSystem* instance;

	/**
	 * @brief Creates a new editor render system instance.
	 * @param[in,out] manager manager instance
	 */
	EditorRenderSystem(Manager* manager);
	/**
	 * @brief Destroys editor render system instance.
	 */
	~EditorRenderSystem();

	void showMainMenuBar();
	void showAboutWindow();
	void showOptionsWindow();
	void showEntityInspector();
	void showNewScene();
	void showExportScene();
	void showFileSelector();

	void init();
	void deinit();
	void editorRender();
	
	friend class ecsm::Manager;
	friend class HierarchyEditorSystem;
public:
	Aabb selectedEntityAabb;
	ID<Entity> selectedEntity;

	template<typename T = Component>
	void registerEntityInspector(OnComponent onComponent)
	{
		if (!entityInspectors.emplace(typeid(T), onComponent).second)
		{
			throw runtime_error("This component type is already registered. ("
				"name: " + typeToString<T>() + ")");
		}
	}
	template<typename T = Component>
	void unregisterEntityInspector()
	{
		if (entityInspectors.erase(typeid(T)) == 0)
		{
			throw runtime_error("This component type is not registered. ("
				"name: " + typeToString<T>() + ")");
		}
	}
	template<typename T = Component>
	bool tryUnregisterEntityInspector()
	{
		return entityInspectors.erase(typeid(T)) != 0;
	}

	void openFileSelector(const std::function<void(const fs::path&)>& onSelect,
		const fs::path& directory = {}, const vector<string>& extensions = {});

	/**
	 * @brief Returns editor render system instance.
	 * @warning Do not use it if you have several managers.
	 */
	static EditorRenderSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
#endif
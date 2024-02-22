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

#define DATA_SAMPLE_BUFFER_SIZE 512

namespace garden
{

using namespace ecsm;
using namespace garden;
using namespace garden::graphics;

/**
 * @brief Base editor system class.
 * @tparam T type of the target system (ex. GraphicsSystem)
 */
template<class T>
class EditorSystem : public System
{
protected:
	T* system = nullptr;

	/**
	 * @brief Creates a new editor system instance.
	 * 
	 * @param[in] manager valid manager instance
	 * @param[in] system valid target system instance
	 */
	EditorSystem(Manager* manager, T* system) : System(manager)
	{
		this->system = system;
	}
};

/***********************************************************************************************************************
 * @brief Editor GUI rendering system.
 * 
 * @details
 * Editor is the suite of tools and interfaces provided by a game engine for creating and editing digital content, 
 * including video games and interactive media. It encompasses tools for building scenes, managing digital assets like 
 * models and textures, scripting behavior, testing the game within the editor, and designing user interfaces.
 * 
 * Registers events: RenderEditor, EditorBarTool.
 */
class EditorRenderSystem final : public System
{
	map<type_index, function<void(ID<Entity>)>> entityInspectors;
	void* hierarchyEditor = nullptr;
	void* resourceEditor = nullptr;
	string scenePath = "unnamed";
	bool demoWindow = false;
	bool aboutWindow = false;
	bool optionsWindow = false;
	bool newScene = false;
	bool exportScene = false;

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

	void renderEditor();

	friend class ecsm::Manager;
	friend class HierarchyEditorSystem;
public:
	Aabb selectedEntityAabb;
	ID<Entity> selectedEntity;

	template<typename T = Component>
	void registerEntityInspector(function<void(ID<Entity>)> onComponent)
	{
		if (!entityInspectors.emplace(typeid(T), onComponent).second)
		{
			throw runtime_error("This component type is already registered. ("
				"name: " + string(typeid(T).name()) + ")");
		}
	}
	template<typename T = Component>
	void unregisterEntityInspector(function<void(ID<Entity>)> onComponent)
	{
		if (entityInspectors.erase(typeid(T)) == 0)
		{
			throw runtime_error("This component type is not registered. ("
				"name: " + string(typeid(T).name()) + ")");
		}
	}
};

} // namespace garden
#endif
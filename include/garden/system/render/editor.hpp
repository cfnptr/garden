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
#include "garden/system/resource.hpp"

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

	struct Inspector final
	{
		OnComponent onComponent;
		float priority = 0.0f;

		Inspector(const OnComponent& _onComponent, float _priority = 0.0f) :
			onComponent(_onComponent), priority(_priority) { }
	};
private:
	map<type_index, Inspector> entityInspectors;
	string scenePath = "unnamed";
	fs::path fileSelectDirectory;
	fs::path selectedEntry;
	fs::path selectedFile;
	set<string> fileExtensions;
	std::function<void(const fs::path)> onFileSelect;
	bool demoWindow = false;
	bool aboutWindow = false;
	bool optionsWindow = false;
	bool newScene = false;
	bool playing = false;

	static EditorRenderSystem* instance;

	/**
	 * @brief Creates a new editor render system instance.
	 */
	EditorRenderSystem();
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
	ID<Entity> selectedEntity;

	bool exportScene = false;

	template<typename T = Component>
	void registerEntityInspector(OnComponent onComponent, float priority = 0.0f)
	{
		Inspector inspector(onComponent, priority);
		if (!entityInspectors.emplace(typeid(T), std::move(inspector)).second)
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

	bool isPlaying() const noexcept { return playing; }
	void setPlaying(bool isPlaying);

	void openFileSelector(const std::function<void(const fs::path&)>& onSelect,
		const fs::path& directory = {}, const set<string>& extensions = {});
	void drawImageSelector(string& path, Ref<Image>& image, Ref<DescriptorSet>& descriptorSet,
		ID<Entity> entity, type_index componentType, ImageLoadFlags loadFlags = {});

	void drawResource(ID<Buffer> buffer);
	void drawResource(ID<Image> image);
	void drawResource(ID<ImageView> imageView);
	void drawResource(ID<Framebuffer> framebuffer);
	void drawResource(ID<DescriptorSet> descriptorSet);
	void drawResource(ID<GraphicsPipeline> graphicsPipeline);
	void drawResource(ID<ComputePipeline> computePipeline);

	void drawResource(const Ref<Buffer>& buffer) { drawResource(ID<Buffer>(buffer)); }
	void drawResource(const Ref<Image>& image) { drawResource(ID<Image>(image)); }
	void drawResource(const Ref<ImageView>& imageView) { drawResource(ID<ImageView>(imageView)); }
	void drawResource(const Ref<Framebuffer>& framebuffer) { drawResource(ID<Framebuffer>(framebuffer)); }
	void drawResource(const Ref<DescriptorSet>& descriptorSet) { drawResource(ID<DescriptorSet>(descriptorSet)); }
	void drawResource(const Ref<GraphicsPipeline>& graphicsPipeline)
	{
		drawResource(ID<GraphicsPipeline>(graphicsPipeline));
	}
	void drawResource(const Ref<ComputePipeline>& computePipeline)
	{
		drawResource(ID<ComputePipeline>(computePipeline));
	}

	/**
	 * @brief Returns editor render system instance.
	 */
	static EditorRenderSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
#endif
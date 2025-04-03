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
 * @brief Editor GUI render functions.
 */

#pragma once
#include "garden/graphics/sampler.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/resource.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/imgui.hpp"
#include "ecsm.hpp"

namespace garden
{

/**
 * @brief Editor GUI rendering system.
 * 
 * @details
 * Editor is the suite of tools and interfaces provided by a game engine for creating and editing digital content, 
 * including video games and interactive media. It encompasses tools for building scenes, managing digital assets like 
 * models and textures, scripting behavior, testing the game within the editor, and designing user interfaces.
 * 
 * Registers events: EditorPlayStart, EditorPlayStop, EditorBarFile, 
 *   EditorBarCreate, EditorBarTool, EditorBarToolPP, EditorBar, EditorSettings.
 */
class EditorRenderSystem final : public System, public Singleton<EditorRenderSystem>
{
public:
	using OnComponent = std::function<void(ID<Entity> entity, bool isOpened)>;

	struct Inspector final
	{
		OnComponent onComponent;
		float priority = 0.0f;

		Inspector(const OnComponent& onComponent, float priority = 0.0f) :
			onComponent(onComponent), priority(priority) { }
	};
private:
	unordered_map<type_index, Inspector> entityInspectors;
	fs::path exportsScenePath = "unnamed";
	fs::path fileSelectDirectory;
	fs::path selectedEntry;
	fs::path selectedFile;
	set<string> fileExtensions;
	std::function<void(const fs::path)> onFileSelect;
	double lastFps = 0.0;
	bool demoWindow = false;
	bool aboutWindow = false;
	bool optionsWindow = false;
	bool newScene = false;
	bool playing = false;

	/**
	 * @brief Creates a new editor render system instance.
	 * @param setSingleton set system singleton instance
	 */
	EditorRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys editor render system instance.
	 */
	~EditorRenderSystem() final;

	void showMainMenuBar();
	void showAboutWindow();
	void showOptionsWindow();
	void showEntityInspector();
	void showNewScene();
	void showExportScene();
	void showFileSelector();

	void init();
	void deinit();
	void preUiRender();
	
	friend class ecsm::Manager;
	friend class HierarchyEditorSystem;
public:
	ID<Entity> selectedEntity;
	bool exportScene = false;

	template<typename T = Component>
	void registerEntityInspector(OnComponent onComponent, float priority = 0.0f)
	{
		Inspector inspector(onComponent, -priority);
		if (!entityInspectors.emplace(typeid(T), std::move(inspector)).second)
		{
			throw GardenError("This component type is already registered. ("
				"name: " + typeToString<T>() + ")");
		}
	}
	template<typename T = Component>
	void unregisterEntityInspector()
	{
		if (entityInspectors.erase(typeid(T)) == 0)
		{
			throw GardenError("This component type is not registered. ("
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
	void drawFileSelector(fs::path& path, ID<Entity> entity, type_index componentType, 
		const fs::path& directory, const set<string>& extensions);
	void drawImageSelector(fs::path& path, Ref<Image>& image, Ref<DescriptorSet>& descriptorSet,
		ID<Entity> entity, type_index componentType, ImageLoadFlags loadFlags = {});

	void drawResource(ID<Buffer> buffer, const char* label = "Buffer");
	void drawResource(ID<Image> image, const char* label = "Image");
	void drawResource(ID<ImageView> imageView, const char* label = "ImageView");
	void drawResource(ID<Framebuffer> framebuffer, const char* label = "Framebuffer");
	void drawResource(ID<Sampler> sampler, const char* label = "Sampler");
	void drawResource(ID<DescriptorSet> descriptorSet, const char* label = "DescriptorSet");
	void drawResource(ID<GraphicsPipeline> graphicsPipeline, const char* label = "Graphics Pipeline");
	void drawResource(ID<ComputePipeline> computePipeline, const char* label = "Compute Pipeline");

	void drawResource(const Ref<Buffer>& buffer, const char* label = "Buffer")
	{ drawResource(ID<Buffer>(buffer), label); }
	void drawResource(const Ref<Image>& image, const char* label = "Image")
	{ drawResource(ID<Image>(image), label); }
	void drawResource(const Ref<ImageView>& imageView, const char* label = "ImageView")
	{ drawResource(ID<ImageView>(imageView), label); }
	void drawResource(const Ref<Framebuffer>& framebuffer, const char* label = "Framebuffer")
	{ drawResource(ID<Framebuffer>(framebuffer), label); }
	void drawResource(const Ref<Sampler>& sampler, const char* label = "Sampler")
	{ drawResource(ID<Sampler>(sampler), label); }
	void drawResource(const Ref<DescriptorSet>& descriptorSet, const char* label = "DescriptorSet")
	{ drawResource(ID<DescriptorSet>(descriptorSet), label); }
	void drawResource(const Ref<GraphicsPipeline>& graphicsPipeline, const char* label = "Graphics Pipeline")
	{ drawResource(ID<GraphicsPipeline>(graphicsPipeline), label); }
	void drawResource(const Ref<ComputePipeline>& computePipeline, const char* label = "Compute Pipeline")
	{ drawResource(ID<ComputePipeline>(computePipeline), label); }
};

} // namespace garden
#endif
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
#include "garden/system/resource.hpp"

#if GARDEN_EDITOR
#include "garden/graphics/imgui.hpp"
#include "ecsm.hpp"

#include <unordered_map>

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
	using OnFileSelect = std::function<void(const fs::path&)>;

	struct Inspector final
	{
		OnComponent onComponent;
		float priority = 0.0f;

		Inspector(const OnComponent& onComponent, float priority = 0.0f) :
			onComponent(onComponent), priority(priority) { }
	};

	
	using OnComponents = unordered_multimap<float, pair<System*, OnComponent>>;
	using EntityInspectors = tsl::robin_map<type_index, Inspector>;
private:
	OnComponents onComponents;
	EntityInspectors entityInspectors;
	fs::path exportsScenePath = "unnamed";
	fs::path fileSelectDirectory;
	fs::path selectedEntry;
	fs::path selectedFile;
	vector<string_view> fileExtensions;
	OnFileSelect onFileSelect;
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
	//******************************************************************************************************************
	ID<Entity> selectedEntity;
	bool exportScene = false;

	template<typename T = Component>
	void registerEntityInspector(OnComponent onComponent, float priority = 0.0f)
	{
		GARDEN_ASSERT(onComponent);
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

	void openFileSelector(const OnFileSelect& onSelect, const fs::path& directory = {}, 
		const vector<string_view>&  extensions = {});
	void drawFileSelector(const char* name, fs::path& path, ID<Entity> entity, 
		type_index componentType, const fs::path& directory, const vector<string_view>& extensions);
	void drawImageSelector(const char* name, fs::path& path, Ref<Image>& image, Ref<DescriptorSet>& descriptorSet,
		ID<Entity> entity, type_index componentType, uint8 maxMipCount = 1, ImageLoadFlags loadFlags = {});
	void drawModelSelector(const char* name, fs::path& path, Ref<Buffer>& vertexBuffer, Ref<Buffer>& indexBuffer,
		ID<Entity> entity, type_index componentType);

	//******************************************************************************************************************
	void drawResource(ID<Buffer> buffer, const char* label = "Buffer");
	void drawResource(ID<Image> image, const char* label = "Image");
	void drawResource(ID<ImageView> imageView, const char* label = "Image View");
	void drawResource(ID<Framebuffer> framebuffer, const char* label = "Framebuffer");
	void drawResource(ID<Sampler> sampler, const char* label = "Sampler");
	void drawResource(ID<Blas> blas, const char* label = "BLAS");
	void drawResource(ID<Tlas> tlas, const char* label = "TLAS");
	void drawResource(ID<DescriptorSet> descriptorSet, const char* label = "Descriptor Set");
	void drawResource(ID<GraphicsPipeline> graphicsPipeline, const char* label = "Graphics Pipeline");
	void drawResource(ID<ComputePipeline> computePipeline, const char* label = "Compute Pipeline");
	void drawResource(ID<RayTracingPipeline> rayTracingPipeline, const char* label = "Ray Tracing Pipeline");

	void drawResource(const Ref<Buffer>& buffer, const char* label = "Buffer")
	{ drawResource(ID<Buffer>(buffer), label); }
	void drawResource(const Ref<Image>& image, const char* label = "Image")
	{ drawResource(ID<Image>(image), label); }
	void drawResource(const Ref<ImageView>& imageView, const char* label = "Image View")
	{ drawResource(ID<ImageView>(imageView), label); }
	void drawResource(const Ref<Framebuffer>& framebuffer, const char* label = "Framebuffer")
	{ drawResource(ID<Framebuffer>(framebuffer), label); }
	void drawResource(const Ref<Sampler>& sampler, const char* label = "Sampler")
	{ drawResource(ID<Sampler>(sampler), label); }
	void drawResource(const Ref<Blas>& blas, const char* label = "BLAS")
	{ drawResource(ID<Blas>(blas), label); }
	void drawResource(const Ref<Tlas>& tlas, const char* label = "TLAS")
	{ drawResource(ID<Tlas>(tlas), label); }
	void drawResource(const Ref<DescriptorSet>& descriptorSet, const char* label = "Descriptor Set")
	{ drawResource(ID<DescriptorSet>(descriptorSet), label); }
	void drawResource(const Ref<GraphicsPipeline>& graphicsPipeline, const char* label = "Graphics Pipeline")
	{ drawResource(ID<GraphicsPipeline>(graphicsPipeline), label); }
	void drawResource(const Ref<ComputePipeline>& computePipeline, const char* label = "Compute Pipeline")
	{ drawResource(ID<ComputePipeline>(computePipeline), label); }
	void drawResource(const Ref<RayTracingPipeline>& rayTracingPipeline, const char* label = "Ray Tracing Pipeline")
	{ drawResource(ID<RayTracingPipeline>(rayTracingPipeline), label); }

};

} // namespace garden
#endif
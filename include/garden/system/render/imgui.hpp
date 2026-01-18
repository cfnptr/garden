// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
 * @brief Immediate mode GUI functions. (ImGui)
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Immediate mode GUI rendering system. (ImGui)
 *
 * @details
 * Dear ImGui is a bloat-free, platform-agnostic library designed to create functional tools and debugging overlays 
 * within real-time 3D applications. Unlike traditional "retained mode" UI systems that store persistent widget 
 * hierarchies and states, ImGui follows an immediate mode paradigm where the UI is defined and rendered every frame 
 * alongside the game’s logic.
 */
class ImGuiRenderSystem final : public System, public Singleton<ImGuiRenderSystem>
{
public:
	struct PushConstants final
	{
		float2 scale;
		float2 translate;
	};

	static constexpr string_view defaultFontPath = "fonts/dejavu-sans-mono/regular.ttf";
private:
	fs::path fontPath;
	vector<ID<Buffer>> vertexBuffers;
	vector<ID<Buffer>> indexBuffers;
	tsl::robin_map<ID<ImageView>, ID<DescriptorSet>> dsCache;
	float2 lastValidMousePos = float2::zero;
	ID<GraphicsPipeline> pipeline = {};
	ID<Sampler> linearSampler = {};
	ID<Sampler> nearestSampler = {};
	CursorType lastCursorType = CursorType::Default;
	bool isInitialized = false;
	bool isRendered = true;

	/**
	 * @brief Creates a new immediate mode GUI rendering system instance. (ImGui)
	 * 
	 * @param setSingleton set system singleton instance
	 * @param[in] fontPath path to the font file
	 */
	ImGuiRenderSystem(bool setSingleton = true, const fs::path& fontPath = defaultFontPath);
	/**
	 * @brief Destroys immediate mode GUI rendering system instance. (ImGui)
	 */
	~ImGuiRenderSystem() final;

	void preInit();
	void postInit();
	void postDeinit();
	void input();
	void update();
	void postLdrToUI();
	void uiRender();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is immediate mode GUI rendering enabled. */

	/**
	 * @brief Returns immediate mode GUI graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
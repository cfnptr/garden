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
 * @brief Immediate mode GUI functions. (ImGui)
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Immediate mode GUI rendering system. (ImGui)
 */
class ImGuiRenderSystem final : public System, public Singleton<ImGuiRenderSystem>
{
public:
	struct PushConstants final
	{
		float2 scale;
		float2 translate;
	};
private:
	fs::path fontPath;
	vector<ID<Buffer>> vertexBuffers;
	vector<ID<Buffer>> indexBuffers;
	float2 lastValidMousePos = float2(0.0f);
	ID<GraphicsPipeline> pipeline = {};
	ID<Image> fontTexture = {};
	ID<DescriptorSet> fontDescriptorSet = {};
	bool startedFrame = false;

	/**
	 * @brief Creates a new immediate mode GUI rendering system instance. (ImGui)
	 * 
	 * @param setSingleton set system singleton instance
	 * @param[in] fontPath path to the font file
	 */
	ImGuiRenderSystem(bool setSingleton = true, const fs::path& fontPath = "fonts/dejavu-regular.ttf");
	/**
	 * @brief Destroys immediate mode GUI rendering system instance. (ImGui)
	 */
	~ImGuiRenderSystem() final;

	void preInit();
	void postDeinit();
	void input();
	void update();
	void present();
	void swapchainRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is immediate mode GUI rendering enabled. */

	/**
	 * @brief Returns immediate mode GUI graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
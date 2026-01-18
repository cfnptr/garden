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
 * @brief Blur rendering functions.
 * @details Based on this: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Blur camera effect rendering system.
 *
 * @details
 * Blur is a post-processing technique used to soften or smear pixel data to simulate optical phenomena, 
 * enhance immersion, or reduce visual artifacts. Most commonly implemented via a Gaussian or Box filter, 
 * blurring involves sampling neighboring pixels and averaging their color values to create a smoother 
 * transition between edges and details.
 */
class BlurRenderSystem final : public System, public Singleton<BlurRenderSystem>
{
public:
	struct PushConstants final
	{
		float intensity;
	};

	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	ID<GraphicsPipeline> ldrGgxPipeline = {};
	ID<DescriptorSet> ldrGgxDS = {};
	ID<Framebuffer> ldrGgxFramebuffers[2] = {};

	/**
	 * @brief Creates a new blur rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	BlurRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys blur rendering system instance.
	 */
	~BlurRenderSystem() final;

	void init();
	void deinit();
	void preDepthLdrRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	float intensity = 1.0f;
	bool ldrGgxBlur = false;
};

} // namespace garden
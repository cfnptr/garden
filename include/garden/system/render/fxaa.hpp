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
 * @brief Fast approximate anti-aliasing rendering functions. (FXAA)
 * @details Based on this: https://github.com/kosua20/Rendu/blob/master/resources/common/shaders/screens/fxaa.frag
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Fast approximate anti-aliasing rendering system. (FXAA)
 * 
 * @details
 * FXAA is a post-processing anti-aliasing technique used to reduce the jagged edges (aliasing) on rendered 
 * objects in real-time applications, such as video games. Unlike traditional anti-aliasing methods, such as MSAA 
 * (Multisample Anti-Aliasing), which operate on geometry or during rasterization, FXAA is a post-processing filter 
 * applied to the final rendered image. It analyzes the image for high-contrast edges (where aliasing occurs) and 
 * smooths them by blending colors along those edges.
 */
class FxaaRenderSystem final : public System, public Singleton<FxaaRenderSystem>
{
public:
	struct PushConstants final
	{
		float2 invFrameSize;
	};

	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	ID<Framebuffer> framebuffer = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	float subpixelQuality = 0.75f;
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;

	/**
	 * @brief Creates a new fast approximate anti-aliasing rendering system instance. (FXAA)
	 * @param setSingleton set system singleton instance
	 */
	FxaaRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys fast approximate anti-aliasing rendering system instance. (FXAA)
	 */
	~FxaaRenderSystem() final;

	void init();
	void deinit();
	void preUiRender();
	void gBufferRecreate();
	void qualityChange();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is fast approximate anti-aliasing rendering enabled. */

	#if GARDEN_DEBUG || GARDEN_EDITOR
	bool visualize = false; /**< Visualize FXAA detected pixels. (Debug only!) */
	#endif

	/**
	 * @brief Returns FXAA rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Returns FXAA amount of sub-pixel aliasing removal.
	 */
	float getSubpixelQuality() const noexcept { return subpixelQuality; }
	/**
	 * @brief Sets FXAA rendering graphics quality.
	 *
	 * @param quality target graphics quality level
	 * @param subpixelQuality amount of sub-pixel aliasing removal (0.0 - 1.0)
	 */
	void setQuality(GraphicsQuality quality, float subpixelQuality);

	/**
	 * @brief Returns fast approximate anti-aliasing framebuffer.
	 */
	ID<Framebuffer> getFramebuffer();
	/**
	 * @brief Returns fast approximate anti-aliasing graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
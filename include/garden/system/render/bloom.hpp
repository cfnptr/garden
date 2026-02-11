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
 * @brief Bloom (light glow) rendering functions.
 * @details Based on this: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
 */

#pragma once
#include "garden/system/graphics.hpp"

// TODO: we can remove flickering using temporal accumulation.

namespace garden
{

/**
 * @brief Bloom (light glow) rendering system.
 *
 * @details
 * Bloom is a post-processing effect used in game engines to simulate the physical phenomenon of light scattering in 
 * real-world camera lenses or the human eye. It functions by identifying pixels in a rendered frame that exceed a 
 * specific brightness threshold, isolating them into a separate buffer, and applying a series of blur passes to 
 * expand their influence. This blurred "glow" is then composited back onto the original image, creating the 
 * illusion of intense luminosity that "bleeds" into surrounding darker areas.
 */
class BloomRenderSystem final : public System, public Singleton<BloomRenderSystem>
{
public:
	struct PushConstants final
	{
		float threshold;
	};

	static constexpr Image::Format bufferFormat = Image::Format::UfloatB10G11R11;
private:
	ID<GraphicsPipeline> downsamplePipeline = {};
	ID<GraphicsPipeline> upsamplePipeline = {};
	ID<Image> bloomBuffer = {};
	vector<ID<ImageView>> imageViews;
	vector<ID<Framebuffer>> framebuffers;
	vector<ID<DescriptorSet>> descriptorSets;
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;
	bool useThreshold = false;

	/**
	 * @brief Creates a new bloom (light glow) rendering system instance.
	 *
	 * @param useThreshold use bloom color threshold for rendering
	 * @param setSingleton set system singleton instance
	 */
	BloomRenderSystem(bool useThreshold = false, bool setSingleton = true);
	/**
	 * @brief Destroys bloom (light glow) rendering system instance.
	 */
	~BloomRenderSystem() final;

	void init();
	void deinit();
	void preLdrRender();
	void gBufferRecreate();
	void qualityChange();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is bloom rendering enabled. */
	float intensity = 0.004f;
	float threshold = 0.0f;

	/*******************************************************************************************************************
	 * @brief Use color threshold for bloom rendering.
	 */
	bool getUseThreshold() const noexcept { return useThreshold; }
	/**
	 * @brief Sets bloom pipeline constants. (Recreates pipeline!).
	 * @param useThreshold use shadow buffer for rendering
	 */
	void setConsts(bool useThreshold);

	/**
	 * @brief Returns bloom rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Sets bloom rendering graphics quality.
	 * @param quality target graphics quality level
	 */
	void setQuality(GraphicsQuality quality);

	/**
	 * @brief Returns bloom downsample graphics pipeline.
	 */
	ID<GraphicsPipeline> getDownsamplePipeline();
	/**
	 * @brief Returns bloom upsample graphics pipeline.
	 */
	ID<GraphicsPipeline> getUpsamplePipeline();
	/**
	 * @brief Returns bloom buffer.
	 */
	ID<Image> getBloomBuffer();
	/**
	 * @brief Returns bloom buffer image views.
	 */
	const vector<ID<ImageView>>& getImageViews();
	/**
	 * @brief Returns bloom framebuffer array.
	 */
	const vector<ID<Framebuffer>>& getFramebuffers();
};

} // namespace garden
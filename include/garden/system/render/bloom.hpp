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
 * @brief Bloom (light glow) rendering functions.
 * @details Based on this: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Bloom (light glow) rendering system.
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
	bool useAntiFlickering = false;

	/**
	 * @brief Creates a new bloom (light glow) rendering system instance.
	 *
	 * @param useThreshold use bloom color threshold for rendering
	 * @param useAntiFlickering use anti flickering algorithm for rendering (anti fireflies)
	 * @param setSingleton set system singleton instance
	 */
	BloomRenderSystem(bool useThreshold = false, bool useAntiFlickering = true, bool setSingleton = true);
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
	 * @brief Use anti flickering algorithm for bloom rendering. (Anti fireflies)
	 */
	bool getUseAntiFlickering() const noexcept { return useAntiFlickering; }
	/**
	 * @brief Sets bloom pipeline constants. (Recreates pipeline!).
	 *
	 * @param useThreshold use shadow buffer for rendering
	 * @param useAntiFlickering use anti flickering algorithm for rendering (anti fireflies)
	 */
	void setConsts(bool useThreshold, bool useAntiFlickering);

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
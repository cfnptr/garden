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
 * @brief Subpixel morphological anti-aliasing rendering functions. (SMAA)
 * @details Based on this: https://github.com/iryoku/smaa
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Subpixel morphological anti-aliasing rendering system. (SMAA)
 * 
 * @details
 * SMAA is an advanced post-processing antialiasing technique that combines the efficiency of image-based 
 * morphological methods with the accuracy of multisampling. It functions by analyzing the luminance and color 
 * gradients of a frame to detect geometric edges, then applies sophisticated pattern recognition to calculate 
 * the coverage area of pixels, effectively smoothing "jaggies" without the significant performance cost of 
 * hardware-based MSAA.
 */
class SmaaRenderSystem final : public System, public Singleton<SmaaRenderSystem>
{
public:
	struct PushConstants final
	{
		float2 invFrameSize;
		float2 frameSize;
	};

	static constexpr Image::Format edgesBufferFormat = Image::Format::UnormR8G8;
	static constexpr Framebuffer::OutputAttachment::Flags processFbFlags = { true, false, true };
	static constexpr Framebuffer::OutputAttachment::Flags blendFbFlags = { false, true, true };
private:
	Ref<Image> searchLUT = {}, areaLUT = {};
	ID<Image> edgesBuffer = {};
	ID<Framebuffer> edgesFramebuffer = {};
	ID<Framebuffer> weightsFramebuffer = {};
	ID<Framebuffer> blendFramebuffer = {};
	ID<GraphicsPipeline> edgesPipeline = {};
	ID<GraphicsPipeline> weightsPipeline = {};
	ID<GraphicsPipeline> blendPipeline = {};
	ID<DescriptorSet> edgesDS = {}, weightsDS = {}, blendDS = {};
	int32 cornerRounding = 25;
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;

	/**
	 * @brief Creates a new subpixel morphological anti-aliasing rendering system instance. (FXAA)
	 * @param setSingleton set system singleton instance
	 */
	SmaaRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys subpixel morphological anti-aliasing rendering system instance. (FXAA)
	 */
	~SmaaRenderSystem() final;

	void init();
	void deinit();
	void preUiRender();
	void gBufferRecreate();
	void qualityChange();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is subpixel morphological anti-aliasing rendering enabled. */

	#if GARDEN_DEBUG || GARDEN_EDITOR
	bool visualize = false; /**< Visualize SMAA detected pixels. (Debug only!) */
	#endif

	/**
	 * @brief Returns SMAA rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Returns SMAA how much sharp corners will be rounded.
	 */
	int32 getCornerRounding() const noexcept { return cornerRounding; }
	/**
	 * @brief Sets SMAA rendering graphics quality.
	 *
	 * @param quality target graphics quality level
	 * @param cornerRounding how much sharp corners will be rounded (0 - 100)
	 */
	void setQuality(GraphicsQuality quality, int cornerRounding = 25);

	/**
	 * @brief Returns SMAA edges buffer.
	 */
	ID<Image> getEdgesBuffer();

	/**
	 * @brief Returns SMAA edges framebuffer.
	 */
	ID<Framebuffer> getEdgesFramebuffer();
	/**
	 * @brief Returns SMAA weights framebuffer.
	 */
	ID<Framebuffer> getWeightsFramebuffer();
	/**
	 * @brief Returns SMAA blend framebuffer.
	 */
	ID<Framebuffer> getBlendFramebuffer();

	/**
	 * @brief Returns SMAA edges graphics pipeline.
	 */
	ID<GraphicsPipeline> getEdgesPipeline();
	/**
	 * @brief Returns SMAA weights graphics pipeline.
	 */
	ID<GraphicsPipeline> getWeightsPipeline();
	/**
	 * @brief Returns SMAA blend graphics pipeline.
	 */
	ID<GraphicsPipeline> getBlendPipeline();
};

} // namespace garden
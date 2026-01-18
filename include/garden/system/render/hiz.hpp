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
 * @brief Fast approximate anti-aliasing rendering functions. (Hi-Z)
 * @details Based on this: https://miketuritzin.com/post/hierarchical-depth-buffers/
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Hierarchical depth (Z) buffer rendering system. (Hi-Z)
 *
 * @details
 * Hierarchical Z-buffering is a visibility acceleration technique that uses a multi-resolution "pyramid" of depth 
 * values to rapidly cull occluded geometry before it undergoes expensive shading. By downsampling the standard depth 
 * buffer into a series of mipmaps the engine can perform a single depth test against a low-resolution tile to 
 * determine if an entire object or group of triangles is hidden. This hierarchical approach allows the GPU to skip 
 * processing large chunks of hidden geometry with minimal texture fetches, significantly reducing overdraw and 
 * improving performance in complex, depth-heavy scenes.
 */
class HizRenderSystem final : public System, public Singleton<HizRenderSystem>
{
public:
	static constexpr Image::Format bufferFormat = Image::Format::SfloatR16;
private:
	ID<GraphicsPipeline> pipeline = {};
	ID<Image> hizBuffer = {};
	vector<ID<ImageView>> imageViews;
	vector<ID<Framebuffer>> framebuffers;
	vector<ID<DescriptorSet>> descriptorSets;
	bool isInitialized = false;

	/**
	 * @brief Creates a new hierarchical depth (Z) buffer rendering system instance. (Hi-Z)
	 * @param setSingleton set system singleton instance
	 */
	HizRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys hierarchical depth (Z) buffer rendering system instance. (Hi-Z)
	 */
	~HizRenderSystem() final;

	void init();
	void deinit();
	void preHdrRender();
	void gBufferRecreate();

	void downsampleHiz(uint8 levelCount);
	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is hierarchical depth (Z) buffer rendering enabled. */

	/**
	 * @brief Returns hierarchical depth (Z) buffer graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
	/**
	 * @brief Returns hierarchical depth (Z) buffer.
	 */
	ID<Image> getHizBuffer();
	/**
	 * @brief Returns hierarchical depth (Z) buffer image views.
	 */
	const vector<ID<ImageView>>& getImageViews();
	/**
	 * @brief Returns hierarchical depth (Z) buffer framebuffer.
	 */
	const vector<ID<Framebuffer>>& getFramebuffers();
};

} // namespace garden
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
 * @brief Order independent transparency rendering functions. (OIT)
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Order independent transparency rendering system. (OIT)
 *
 * @details
 * Order-independent transparency is a technique used to render overlapping transparent objects correctly without 
 * requiring the CPU to manually sort geometry from back-to-front every frame. Traditional alpha blending is 
 * non-commutative, meaning that if a distant glass bottle is rendered after a closer puff of smoke, the depth 
 * buffer will incorrectly discard the bottle or blend it with "wrong" background data, leading to visual artifacts 
 * like flickering or missing surfaces. OIT resolves this by handling the sorting or blending logic per-pixel on the 
 * GPU to ensure that light transmittance is mathematically accurate even when complex transparent meshes intersect 
 * or rotate around the camera.
 */
class OitRenderSystem final : public System, public Singleton<OitRenderSystem>
{
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};

	/**
	 * @brief Creates a new order independent transparency rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	OitRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys order independent transparency rendering system instance.
	 */
	~OitRenderSystem() final;

	void init();
	void deinit();
	void preLdrRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is order independent transparency rendering enabled. */

	/**
	 * @brief Returns order independent transparency graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
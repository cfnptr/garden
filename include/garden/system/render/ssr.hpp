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
 * @brief Screen space reflections rendering functions.
 * 
 * @details Based on this: TODO
 */

#pragma once
//#include "garden/system/render/pbr-lighting.hpp"

namespace garden
{

/**
 * @brief Screen space reflections rendering system. (SSR)
 * 
 * @details
 * SSR is a real-time rendering technique used to approximate reflections in 3D scenes by using information already 
 * present in the rendered frame, specifically the screen space data. It provides realistic reflections without the 
 * need to ray trace the entire scene. But SSR can only reflect objects visible in the current view. Anything outside 
 * the camera's view or occluded cannot be reflected, leading to artifacts or incomplete reflections.
 */
class SsrRenderSystem final : public System
{
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;

	void render() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
public:
	bool isEnabled = true;

	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
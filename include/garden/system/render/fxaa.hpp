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
 * @brief Fast approximate anti-aliasing rendering functions. (FXAA)
 * @details Based on this: https://github.com/kosua20/Rendu/blob/master/resources/common/shaders/screens/fxaa.frag
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Fast approximate anti-aliasing rendering system. (FXAA)
 */
class FxaaRenderSystem final : public System, public Singleton<FxaaRenderSystem>
{
public:
	struct PushConstants final
	{
		float2 invFrameSize;
	};
private:
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};

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
	void preSwapchainRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is fast approximate anti-aliasing rendering enabled. */

	/**
	 * @brief Returns fast approximate anti-aliasing graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
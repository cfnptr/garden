//--------------------------------------------------------------------------------------------------
// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

// TODO:

#pragma once
//#include "garden/system/render/pbr-lighting.hpp"

namespace garden
{

//--------------------------------------------------------------------------------------------------
// Screen Space Reflection
class SsrRenderSystem final : public System, public IRenderSystem
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
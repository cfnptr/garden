//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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

#pragma once
#include "garden/system/graphics/lighting.hpp"

namespace garden
{

using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// Screen Space Ambient Occlusion
class SsaoRenderSystem final : public System, public IRenderSystem, public IAoRenderSystem
{
private:
	ID<Buffer> sampleBuffer = {};
	ID<Image> noiseTexture = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;

	void render() final;
	bool aoRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
public:
	float radius = 0.5f;
	float bias = 0.025f;
	float intensity = 0.5f;
	bool isEnabled = true;

	ID<Buffer> getSampleBuffer();
	ID<Image> getNoiseTexture();
	ID<GraphicsPipeline> getPipeline();
};

} // garden
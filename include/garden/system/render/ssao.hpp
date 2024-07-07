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
// 
// Based on this: https://lettier.github.io/3d-game-shaders-for-beginners/ssao.html
// And this: https://learnopengl.com/Advanced-Lighting/SSAO
//--------------------------------------------------------------------------------------------------

/*
#pragma once
#include "garden/system/render/lighting.hpp"

namespace garden
{

using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// Screen Space Ambient Occlusion
class SsaoRenderSystem final : public System, public IRenderSystem, public IAoRenderSystem
{
	ID<Buffer> sampleBuffer = {};
	ID<Image> noiseTexture = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	uint32 sampleCount = 32;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;

	void render() final;
	void preAoRender() final;
	bool aoRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
public:
	float radius = 0.5f;
	float bias = 0.025f;
	float intensity = 0.5f;
	bool isEnabled = true;

	uint32 getSampleCount() const noexcept { return sampleCount; }
	void setConsts(uint32 sampleCount);

	ID<Buffer> getSampleBuffer();
	ID<Image> getNoiseTexture();
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
*/
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
// Based on this: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
//--------------------------------------------------------------------------------------------------

/*
#pragma once
#include "garden/system/render/deferred.hpp"

#define MAX_BLOOM_MIP_COUNT 6 // TODO: use constant

namespace garden
{

using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// Bloom aka Light Glow
class BloomRenderSystem final : public System,
	public IRenderSystem, public IDeferredRenderSystem
{
	ID<GraphicsPipeline> downsample0Pipeline = {};
	ID<GraphicsPipeline> downsamplePipeline = {};
	ID<GraphicsPipeline> upsamplePipeline = {};
	ID<Image> bloomBuffer = {};
	vector<ID<ImageView>> imageViews;
	vector<ID<Framebuffer>> framebuffers;
	vector<ID<DescriptorSet>> descriptorSets;
	vector<int2> sizeBuffer;
	bool useThreshold = false;
	bool useAntiFlickering = true;
	uint16 _alignment = 0;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void render() final;
	void preLdrRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
public:
	float intensity = 0.004f;
	float threshold = 0.0f;
	bool isEnabled = true;

	bool getUseThreshold() const noexcept { return useThreshold; }
	bool getUseAntiFlickering() const noexcept { return useAntiFlickering; }
	void setConsts(bool useThreshold, bool useAntiFlickering);

	ID<GraphicsPipeline> getDownsample0Pipeline();
	ID<GraphicsPipeline> getDownsamplePipeline();
	ID<GraphicsPipeline> getUpsamplePipeline();
	ID<Image> getBloomBuffer();
	const vector<ID<Framebuffer>>& getFramebuffers();
};

} // namespace garden
*/
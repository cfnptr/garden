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
#include "garden/system/graphics/bloom.hpp"
#include "garden/system/graphics/auto-exposure.hpp"

namespace garden
{

#define LUM_TO_EXP 9.6f

using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
class ToneMappingRenderSystem final : public System,
	public IRenderSystem, public IDeferredRenderSystem
{
public:
	struct Luminance
	{
		float avgLuminance = 0.0f;
		float exposure = 0.0f;
	};
private:
	BloomRenderSystem* bloomSystem = nullptr;
	AutoExposureRenderSystem* autoExposureSystem = nullptr;
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void render() final;
	void ldrRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
	friend class ToneMappingEditor;
public:
	float exposureCoeff = 1.0f;
	float ditherStrength = (0.5f / 255.0f); // r8g8b8

	ID<GraphicsPipeline> getPipeline();
};

} // garden
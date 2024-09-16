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

#pragma once
#include "garden/system/render/auto-exposure.hpp"

#if GARDEN_EDITOR
namespace garden
{

using namespace garden::graphics;

class AutoExposureEditor final
{
	ID<Buffer> readbackBuffer = {};
	ID<GraphicsPipeline> limitsPipeline = {};
	ID<DescriptorSet> limitsDescriptorSet = {};
	float* histogramSamples = nullptr;
	bool visualizeLimits = false;
	bool showWindow = false;

	AutoExposureEditor(AutoExposureRenderSystem* system);
	~AutoExposureEditor() final;

	void render();
	void recreateSwapchain(const IRenderSystem::SwapchainChanges& changes);
	void onBarTool();
	
	friend class AutoExposureRenderSystem;
};

} // namespace garden
#endif
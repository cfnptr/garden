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
// Based on this: https://bruop.github.io/exposure/
//--------------------------------------------------------------------------------------------------

// TODO: possibly can be improved by using more luminance weight at screen center.

/*
#pragma once
#include "garden/system/render/deferred.hpp"

// Human eye adaptation:
// Full darkness - 20 min.
// Full brightness = 5 min.

namespace garden
{

#define AE_HISTOGRAM_SIZE 256

using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// AE - Automatic Exposure aka Eye Light Adaptation
class AutoExposureRenderSystem final : public System, public IRenderSystem
{
	DeferredRenderSystem* deferredSystem = nullptr;
	ID<ComputePipeline> histogramPipeline = {};
	ID<ComputePipeline> averagePipeline = {};
	ID<DescriptorSet> histogramDescriptorSet = {};
	ID<DescriptorSet> averageDescriptorSet = {};
	ID<Buffer> histogramBuffer = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void render() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
	friend class AutoExposureEditorSystem;
public:
	float minLogLum = -8.0f;
	float maxLogLum = 4.0f;
	float darkAdaptRate = 1.0f;
	float brightAdaptRate = 3.0f;
	bool isEnabled = true;

	ID<ComputePipeline> getHistogramPipeline();
	ID<ComputePipeline> getAveragePipeline();
	ID<Buffer> getHistogramBuffer();
};

} // namespace garden
*/
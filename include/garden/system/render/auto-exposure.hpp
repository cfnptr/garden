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
 * @brief Automatic exposure (AE) rendering functions. (Eye light adaptation)
 * 
 * @details
 * Based on this: https://bruop.github.io/exposure/
 * 
 * Human eye adaptation:
 *   Full darkness = 20 min.
 *   Full brightness = 5 min.
 */

// TODO: possibly can be improved by using more luminance weight at screen center.

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Automatic exposure (AE) rendering system. (Eye light adaptation)
 *
 * @details
 * Automatic Exposure (also known as Eye Adaptation) is a post-processing effect that dynamically simulates how the 
 * human eye or a physical camera lens adjusts to varying light intensities within a scene. By analyzing the average 
 * luminance of the current frame the engine automatically calculates and applies a compensation factor to the 
 * final render. This ensures that details remain visible when transitioning between dimly lit interiors and bright 
 * outdoor environments, preventing the image from becoming "blown out" or too dark.
 */
class AutoExposureSystem final : public System, public Singleton<AutoExposureSystem>
{
public:
	struct HistogramPC final
	{
		float minLogLum;
		float invLogLumRange;
	};
	struct AveragePC final
	{
		float minLogLum;
		float logLumRange;
		float pixelCount;
		float darkAdaptRate;
		float brightAdaptRate;
	};

	/**
	 * @brief Automatic exposure histogram buffer size.
	 */
	static constexpr uint16 histogramSize = 256;
private:
	ID<ComputePipeline> histogramPipeline = {};
	ID<ComputePipeline> averagePipeline = {};
	ID<DescriptorSet> histogramDS = {};
	ID<DescriptorSet> averageDS = {};
	ID<Buffer> histogramBuffer = {};
	uint16 _alignment = 0;
	bool isInitialized = false;

	/**
	 * @brief Creates a new automatic exposure (AE) rendering system instance. (Eye light adaptation)
	 * @param setSingleton set system singleton instance
	 */
	AutoExposureSystem(bool setSingleton = true);
	/**
	 * @brief Destroys automatic exposure (AE) rendering system instance. (Eye light adaptation)
	 */
	~AutoExposureSystem() final;

	void init();
	void deinit();
	void render();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is automatic exposure rendering enabled. */
	float minLogLum = -8.0f;
	float maxLogLum = 4.0f;
	float darkAdaptRate = 0.5f;
	float brightAdaptRate = 0.5f;

	/**
	 * @brief Returns automatic exposure histogram compute pipeline.
	 */
	ID<ComputePipeline> getHistogramPipeline();
	/**
	 * @brief Returns automatic exposure average compute pipeline.
	 */
	ID<ComputePipeline> getAveragePipeline();
	/**
	 * @brief Returns automatic exposure histogram buffer.
	 */
	ID<Buffer> getHistogramBuffer();
};

} // namespace garden
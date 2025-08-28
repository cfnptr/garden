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
 * @brief Tone mapping rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"
#include "tone-mapping/functions.h"

namespace garden
{

/**
 * @brief Tone mapping rendering system.
 */
class ToneMappingSystem final : public System, public Singleton<ToneMappingSystem>
{
public:
	struct PushConstants final
	{
		uint32 frameIndex;
		float exposureFactor;
		float ditherIntensity;
		float bloomIntensity;
	};
	struct LuminanceData final
	{
		float avgLuminance = 0.0f;
		float exposure = 0.0f;
	};

	/**
	 * @brief Luminance to exposure conversion coefficient.
	 */
	static constexpr float lumToExp = 9.6f;
private:
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	ID<Buffer> luminanceBuffer = {};
	bool useBloomBuffer = false;
	bool lastUpscaleState = false;
	uint8 toneMapper = TONE_MAPPER_ACES;
	uint8 _alignment = 0;

	/**
	 * @brief Creates a new tone mapping rendering system instance.
	 *
	 * @param useBloomBuffer use bloom (light glow) buffer for tone mapping
	 * @param toneMapper target tone mapping function
	 * @param setSingleton set system singleton instance
	 */
	ToneMappingSystem(bool useBloomBuffer = false, 
		uint8 toneMapper = TONE_MAPPER_ACES, bool setSingleton = true);
	/**
	 * @brief Destroys tone mapping rendering system instance.
	 */
	~ToneMappingSystem() final;

	void init();
	void deinit();
	void ldrRender();
	void dsRecreate();

	friend class ecsm::Manager;
public:
	float exposureFactor = 1.0f;
	float ditherIntensity = (0.5f / 255.0f); /**< (255 for R8G8B8 format) */

	/**
	 * @brief Use bloom (light glow) buffer for tone mapping.
	 */
	bool getUseBloomBuffer() const noexcept { return useBloomBuffer; }
	/**
	 * @brief Returns current tone mapping function.
	 */
	uint8 getToneMapper() const noexcept { return toneMapper; }
	/**
	 * @brief Sets tone mapping pipeline constants. (Recreates pipeline!)
	 * 
	 * @param useBloomBuffer use bloom (light glow) buffer for tone mapping
	 * @param toneMapper target tone mapping function
	 */
	void setConsts(bool useBloomBuffer, uint8 toneMapper);

	/**
	 * @brief Returns tone mapping graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
	/**
	 * @brief Returns tone mapping luminance buffer.
	 */
	ID<Buffer> getLuminanceBuffer();

	/**
	 * @brief Sets tone mapping luminance value.
	 * @param luminance target luminance value
	 */
	void setLuminance(float luminance);
	/**
	 * @brief Sets tone mapping exposure value.
	 * @param luminance target exposure value
	 */
	void setExposure(float exposure);
};

} // namespace garden
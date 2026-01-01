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
 * @brief Screen space horizon based ambient occlusion rendering functions.
 * @details Based on this: https://github.com/nvpro-samples/gl_ssao
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Screen space horizon-based ambient occlusion rendering system. (HBAO)
 */
class HbaoRenderSystem final : public System, public Singleton<HbaoRenderSystem>
{
public:
	struct PushConstants final
	{
		float4 projInfo;
		float2 invFullRes;
		float negInvR2;
		float radiusToScreen;
		float powExponent;
		float novBias;
		float aoMultiplier;
		uint32 projOrtho;
		float nearPlane;
	};
private:
	ID<Image> noiseImage = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	uint32 stepCount = 4;
	uint16 _alignment = 0;
	bool isInitialized = false;

	/**
	 * @brief Creates a new screen space horizon-based ambient occlusion rendering system instance. (HBAO)
	 * @param setSingleton set system singleton instance
	 */
	HbaoRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys screen space horizon-based ambient occlusion rendering system instance. (HBAO)
	 */
	~HbaoRenderSystem() final;

	void init();
	void deinit();
	void preAoRender();
	void aoRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is screen space horizon-based ambient occlusion rendering enabled. */
	float radius = 0.5f;
	float bias = 0.4f;
	float intensity = 1.0f;

	/**
	 * @brief Returns screen space horizon-based ambient occlusion step count.
	 */
	uint32 getStepCount() const noexcept { return stepCount; }
	/**
	 * @brief Sets screen space horizon-based ambient occlusion step count.
	 */
	void setConsts(uint32 stepCount);

	/**
	 * @brief Returns screen space horizon-based ambient occlusion sample buffer.
	 */
	ID<Buffer> getSampleBuffer();
	/**
	 * @brief Returns screen space horizon-based ambient occlusion noise image.
	 */
	ID<Image> getNoiseImage();
	/**
	 * @brief Returns screen space horizon-based ambient occlusion graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
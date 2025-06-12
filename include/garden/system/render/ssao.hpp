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
 * @brief Screen space ambient occlusion rendering functions.
 * 
 * @details Based on these:
 * https://lettier.github.io/3d-game-shaders-for-beginners/ssao.html
 * https://learnopengl.com/Advanced-Lighting/SSAO
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Screen space ambient occlusion rendering system. (SSAO)
 * 
 * @details
 * SSAO is a rendering technique used to approximate ambient occlusion, which is a shading method that simulates how 
 * light is blocked or occluded by surrounding geometry. It enhances depth perception and realism by creating subtle 
 * shadows in areas where objects are close together or where light has limited reach, such as corners, creases, or 
 * spaces between objects. SSAO operates in screen space, meaning it uses information only from what is visible in 
 * the current camera view, specifically the depth buffer and the normal buffer.
 */
class SsaoRenderSystem final : public System, public Singleton<SsaoRenderSystem>
{
public:
	struct PushConstants final
	{
		float4x4 uvToView;
		float4x4 viewToUv;
	};
private:
	ID<Buffer> sampleBuffer = {};
	ID<Image> noiseTexture = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	uint32 sampleCount = 32;
	bool isInitialized = false;
	uint16 _alignment = 0;

	/**
	 * @brief Creates a new screen space ambient occlusion rendering system instance. (SSAO)
	 * @param setSingleton set system singleton instance
	 */
	SsaoRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys screen space ambient occlusion rendering system instance. (SSAO)
	 */
	~SsaoRenderSystem() final;

	void init();
	void deinit();
	void preAoRender();
	void aoRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is screen space ambient occlusion rendering enabled. */
	float radius = 0.5f;
	float bias = 0.025f;
	float intensity = 0.75f;

	/**
	 * @brief Returns screen space ambient occlusion sample buffer size.
	 */
	uint32 getSampleCount() const noexcept { return sampleCount; }
	/**
	 * @brief Sets screen space ambient occlusion sample buffer size.
	 */
	void setConsts(uint32 sampleCount);

	/**
	 * @brief Returns screen space ambient occlusion sample buffer.
	 */
	ID<Buffer> getSampleBuffer();
	/**
	 * @brief Returns screen space ambient occlusion noise texture.
	 */
	ID<Image> getNoiseTexture();
	/**
	 * @brief Returns screen space ambient occlusion graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
};

} // namespace garden
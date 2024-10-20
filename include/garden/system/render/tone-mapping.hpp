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

/***********************************************************************************************************************
 * @file
 * @brief Tone mapping rendering functions.
 */

 /*
#pragma once
#include "garden/system/render/bloom.hpp"


namespace garden
{


constexpr float lumToExp = 9.6f;


enum class ToneMapper : uint8
{
	ACES, Uchimura, Count
};


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
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	ID<Buffer> luminanceBuffer = {};
	bool useBloomBuffer = false;
	ToneMapper toneMapper = {};
	uint16 _alignment1 = 0;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void render() final;
	void preLdrRender() final;
	void ldrRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;

	friend class ecsm::Manager;
public:
	float exposureCoeff = 1.0f;
	float ditherIntensity = (0.5f / 255.0f); // r8g8b8

	bool getUseBloomBuffer() const noexcept { return useBloomBuffer; }
	ToneMapper getToneMapper() const noexcept { return toneMapper; }
	void setConsts(bool useBloomBuffer, ToneMapper toneMapper);

	ID<GraphicsPipeline> getPipeline();
	ID<Buffer> getLuminanceBuffer();

	void setLuminance(float luminance);
	void setExposure(float exposure);
};


} // namespace garden
*/
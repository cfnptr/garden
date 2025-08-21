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
 * @brief Physically based atmosphere rendering functions.
 * 
 * @details
 * Based on this paper: https://sebh.github.io/publications/egsr2020.pdf
 * Precomputed values: https://github.com/ebruneton/precomputed_atmospheric_scattering/blob/master/atmosphere/demo/demo.cc
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Physically based atmosphere rendering system.
 */
class AtmosphereRenderSystem final : public System, public Singleton<AtmosphereRenderSystem>
{
public:
	struct TransmittancePC final
	{
		float3 rayleighScattering;
		float rayDensityExpScale;
		float3 mieExtinction;
		float mieDensityExpScale;
		float3 absorptionExtinction;
		float absDensity0LayerWidth;
		float3 sunDir;
		float absDensity0ConstantTerm;
		float absDensity0LinearTerm;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
	};
	struct MultiScatPC final
	{
		float3 rayleighScattering;
		float rayDensityExpScale;
		float3 mieExtinction;
		float mieDensityExpScale;
		float3 absorptionExtinction;
		float miePhaseG;
		float3 mieScattering;
		float absDensity0LayerWidth;
		float3 groundAlbedo;
		float absDensity0ConstantTerm;
		float absDensity0LinearTerm;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
		float multiScatFactor;
	};
	struct CameraVolumePC final
	{
		float3 rayleighScattering;
		float rayDensityExpScale;
		float3 mieExtinction;
		float mieDensityExpScale;
		float3 absorptionExtinction;
		float miePhaseG;
		float3 mieScattering;
		float absDensity0LayerWidth;
		float3 sunDir;
		float absDensity0ConstantTerm;
		float3 cameraPos;
		float absDensity0LinearTerm;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
	};
	struct SkyViewPC final
	{
		float3 rayleighScattering;
		float rayDensityExpScale;
		float3 mieExtinction;
		float mieDensityExpScale;
		float3 absorptionExtinction;
		float miePhaseG;
		float3 mieScattering;
		float absDensity0LayerWidth;
		float3 sunDir;
		float absDensity0ConstantTerm;
		float3 cameraPos;
		float absDensity0LinearTerm;
		float2 skyViewLutSize;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
	};

	static constexpr uint2 transLutSize = uint2(256, 64);
	static constexpr uint32 multiScatLutLength = 32;
	static constexpr uint32 cameraVolumeLength = 32;
private:
	ID<Image> transLUT = {};
	ID<Image> multiScatLUT = {};
	ID<Image> cameraVolume = {};
	ID<Image> skyViewLUT = {};
	ID<Framebuffer> transLutFramebuffer = {};
	ID<Framebuffer> skyViewLutFramebuffer = {};
	ID<Framebuffer> skyboxFramebuffers[6] = {};
	ID<GraphicsPipeline> transLutPipeline = {};
	ID<ComputePipeline> multiScatLutPipeline = {};
	ID<ComputePipeline> cameraVolumePipeline = {};
	ID<GraphicsPipeline> skyViewLutPipeline = {};
	ID<DescriptorSet> multiScatLutDS = {};
	ID<DescriptorSet> cameraVolumeDS = {};
	ID<DescriptorSet> skyViewLutDS = {};
	bool isInitialized = false;

	/**
	 * @brief Creates a new physically based atmosphere rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	AtmosphereRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys physically based atmosphere rendering system instance.
	 */
	~AtmosphereRenderSystem() final;

	void init();	
	void deinit();
	void preDeferredRender();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;
	float3 rayleighScattering = float3(0.005802f, 0.013558f, 0.033100f);
	float rayleightScaleHeight = 8.0f; /**< (km) */
	float3 mieScattering = float3(0.003996f);
	float mieScaleHeight = 1.2f; /**< (km) */
	float3 mieAbsorption = float3(0.000444f);
	float miePhaseG = 0.8f;
	float3 ozoneAbsorption = float3(0.000650f, 0.001881f, 0.000085f);
	float ozoneLayerWidth = 25.0f; /**< (km) */
	float ozoneLayerSlope = 1.0f / 15.0f;
	float ozoneLayerTip = 1.0f;
	float3 groundAlbedo = float3(0.4f);
	float groundRadius = 6371.0f; /**< (km) */
	float atmosphereHeight = 60.0f; /**< (km) */
	float multiScatFactor = 1.0f;

	/**
	 * @brief Returns atmosphere transmittance LUT. (Look Up Table)
	 */
	ID<Image> getTransLUT();
	/**
	 * @brief Returns atmosphere multiple scattering LUT. (Look Up Table)
	 */
	ID<Image> getMultiScatLUT();
	/**
	 * @brief Returns atmosphere camera volume scattering LUT. (Look Up Table)
	 */
	ID<Image> getCameraVolume();
	/**
	 * @brief Returns atmosphere sky view LUT. (Look Up Table)
	 */
	ID<Image> getSkyViewLUT();
};

} // namespace garden
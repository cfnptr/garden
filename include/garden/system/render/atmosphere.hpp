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
 * @brief Physically based atmosphere rendering system. (Sky)
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
	struct SkyPushConstants final
	{
		float4x4 invViewProj;
		float3 cameraPos;
		float bottomRadius;
		float3 sunDir;
		float topRadius;
		float3 sunColor;
		float sunSize;
	};

	static constexpr float3 earthRayleighScattering = float3(0.005802f, 0.013558f, 0.033100f);
	static constexpr float earthRayleightScaleHeight = 8.0f;
	static constexpr float3 earthMieScattering = float3(0.003996f);
	static constexpr float earthMieScaleHeight = 1.2f;
	static constexpr float3 earthMieAbsorption = float3(0.000444f);
	static constexpr float earthMiePhaseG = 0.8f;
	static constexpr float3 earthOzoneAbsorption = float3(0.000650f, 0.001881f, 0.000085f);
	static constexpr float earthOzoneLayerWidth = 25.0f;
	static constexpr float earthOzoneLayerSlope = 1.0f / 15.0f;
	static constexpr float earthOzoneLayerTip = 1.0f;
	static constexpr float3 earthGroundAlbedo = float3(0.4f);
	static constexpr float earthGroundRadius = 6371.0f;
	static constexpr float earthAtmosphereHeight = 60.0f;
	static constexpr float earthSunAngularSize = 0.53f;

	// TOOD: visually adjust values or use better source
	static constexpr float3 marsRayleighScattering = float3(0.00008703f, 0.00020337f, 0.00049650f);
	static constexpr float marsRayleightScaleHeight = 11.1f;
	static constexpr float3 marsMieScattering = float3(0.0120f, 0.01f, 0.007f);
	static constexpr float marsMieScaleHeight = 10.0f;
	static constexpr float3 marsMieAbsorption = float3(0.0008f, 0.0006f, 0.0004f);
	static constexpr float marsMiePhaseG = 0.7f;
	static constexpr float3 marsOzoneAbsorption = float3(0.0f);
	static constexpr float3 marsGroundAlbedo = float3(0.16f);
	static constexpr float marsGroundRadius = 3389.5f;
	static constexpr float marsAtmosphereHeight = 80.0f;
	static constexpr float marsSunAngularSize = 0.35f;

	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	vector<float> iblWeightBuffer;
	vector<uint32> iblCountBuffer;
	vector<ID<ImageView>> specularViews;
	vector<ID<DescriptorSet>> iblDescriptorSets;
	DescriptorSet::Buffers shCaches, shStagings;
	ID<Image> transLUT = {}, multiScatLUT = {};
	ID<Image> cameraVolume = {}, skyViewLUT = {};
	ID<Buffer> specularCache = {};
	ID<Framebuffer> transLutFramebuffer = {};
	ID<Framebuffer> skyViewLutFramebuffer = {};
	ID<GraphicsPipeline> transLutPipeline = {};
	ID<ComputePipeline> multiScatLutPipeline = {};
	ID<ComputePipeline> cameraVolumePipeline = {};
	ID<GraphicsPipeline> skyViewLutPipeline = {};
	ID<GraphicsPipeline> hdrSkyPipeline = {};
	ID<GraphicsPipeline> skyboxPipeline = {};
	ID<ComputePipeline> shSkyPipeline = {};
	ID<DescriptorSet> multiScatLutDS = {}, cameraVolumeDS = {};
	ID<DescriptorSet> skyViewLutDS = {}, hdrSkyDS = {};
	ID<DescriptorSet> skyboxDS = {}, shSkyDS = {};
	ID<ImageView> skyboxViews[Image::cubemapFaceCount] = {};
	ID<Framebuffer> skyboxFramebuffers[Image::cubemapFaceCount] = {};
	Ref<Image> lastSkybox = {}, lastSpecular = {};
	ID<ImageView> lastSkyboxShView = {};
	float3 sunDir = float3::top;
	uint32 lastSkyboxSize = 0, shInFlightIndex = 0;
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;
	uint8 updatePhase = 0;

	/**
	 * @brief Creates a new physically based atmosphere rendering system instance. (Sky)
	 * @param setSingleton set system singleton instance
	 */
	AtmosphereRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys physically based atmosphere rendering system instance. (Sky)
	 */
	~AtmosphereRenderSystem() final;

	void init();	
	void deinit();
	void preDeferredRender();
	void hdrRender();
	void gBufferRecreate();
	void qualityChange();
	void updateSkybox();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is physically based atmosphere rendering enabled. */
	float3 rayleighScattering = earthRayleighScattering;
	float rayleightScaleHeight = earthRayleightScaleHeight; /**< (km) */
	float3 mieScattering = earthMieScattering;
	float mieScaleHeight = earthMieScaleHeight; /**< (km) */
	float3 mieAbsorption = earthMieAbsorption;
	float miePhaseG = earthMiePhaseG;
	float3 ozoneAbsorption = earthOzoneAbsorption;
	float ozoneLayerWidth = earthOzoneLayerWidth; /**< (km) */
	float ozoneLayerSlope = earthOzoneLayerSlope;
	float ozoneLayerTip = earthOzoneLayerTip;
	float3 groundAlbedo = earthGroundAlbedo;
	float groundRadius = earthGroundRadius; /**< (km) */
	float atmosphereHeight = earthAtmosphereHeight; /**< (km) */
	float4 sunColor = float4(float3(1.0f), 64000.0f);
	float sunAngularSize = earthSunAngularSize; /**< (degrees) */
	float multiScatFactor = 1.0f;

	/**
	 * @brief Returns atmosphere rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Sets atmosphere rendering graphics quality.
	 * @param quality target graphics quality level
	 */
	void setQuality(GraphicsQuality quality);

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
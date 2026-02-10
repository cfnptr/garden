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
 * @brief Physically based atmosphere rendering functions.
 * 
 * @details
 * Based on this paper: https://sebh.github.io/publications/egsr2020.pdf
 * Precomputed values: https://github.com/ebruneton/precomputed_atmospheric_scattering/blob/master/atmosphere/demo/demo.cc
 */

// TODO: out of space atmosphere rendering. We need to ray trace it, can't reuse LUTs.

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Physically based atmosphere rendering system. (Sky)
 *
 * @details
 * Physically based atmosphere rendering is a simulation technique that uses the principles of light physics to 
 * recreate the appearance of the sky and aerial perspective rather than relying on artistic gradients or simple fog. 
 * By modeling the interactions between sunlight and particles in a virtual planetary volume, the system calculates 
 * Rayleigh scattering (the redirection of light by small air molecules, creating blue skies and red sunsets) and 
 * Mie scattering (the interaction with larger aerosols like dust or moisture, creating solar halos and haze).
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
		float3 starDir;
		float absDensity0ConstantTerm;
		float absDensity0LinearTerm;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
	};
	struct MultiScattPC final
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
		float multiScattFactor;
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
		float3 starDir;
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
		float3 starDir;
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
		float3 starDir;
		float topRadius;
		float3 starColor;
		float starSize;
	};
	struct ShReducePC final
	{
		uint32 offset;
	};

	static constexpr float3 earthRayleighScattering = float3(0.005802f, 0.013558f, 0.0331f);
	static constexpr float earthRayleightScaleHeight = 8.0f;
	static constexpr float3 earthMieScattering = float3(0.003996f);
	static constexpr float earthMieScaleHeight = 1.2f;
	static constexpr float3 earthMieAbsorption = float3(0.000444f);
	static constexpr float earthMiePhaseG = 0.8f;
	static constexpr float3 earthOzoneAbsorption = float3(0.00065f, 0.001881f, 0.000085f);
	static constexpr float earthOzoneLayerWidth = 25.0f;
	static constexpr float earthOzoneLayerSlope = 1.0f / 15.0f;
	static constexpr float earthOzoneLayerTip = 1.0f;
	static constexpr float3 earthGroundAlbedo = float3(0.4f);
	static constexpr float earthGroundRadius = 6371.0f;
	static constexpr float earthAtmosphereHeight = 60.0f;
	static constexpr float earthSunAngularSize = 0.53f;

	// Mars has a very thin atmosphere (CO2), so Rayleigh is weak.
	// However, the sky is bright due to suspended dust (Mie).
	static constexpr float3 marsRayleighScattering = float3(0.000087f, 0.000203f, 0.000496f);
	static constexpr float marsRayleightScaleHeight = 11.1f;

	// Mie (Dust) is the dominant factor on Mars.
	static constexpr float3 marsMieScattering = float3(0.08f, 0.06f, 0.04f);
	static constexpr float marsMieScaleHeight = 11.1f;
	static constexpr float3 marsMieAbsorption = float3(0.001f, 0.004f, 0.012f);
	static constexpr float marsMiePhaseG = 0.75f;

	// Mars has negligible Ozone.
	static constexpr float3 marsOzoneAbsorption = float3(0.0f);
	static constexpr float marsOzoneLayerWidth = 0.0f;
	static constexpr float marsOzoneLayerSlope = 0.0f;
	static constexpr float marsOzoneLayerTip = 0.0f;

	static constexpr float3 marsGroundAlbedo = float3(0.25f, 0.15f, 0.1f);
	static constexpr float marsGroundRadius = 3389.5f;
	static constexpr float marsAtmosphereHeight = 100.0f;
	static constexpr float marsSunAngularSize = 0.35f;

	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	//******************************************************************************************************************
	vector<float> iblWeightBuffer;
	vector<uint32> iblCountBuffer;
	vector<ID<ImageView>> specularViews;
	vector<ID<DescriptorSet>> iblDescriptorSets;
	DescriptorSet::Buffers shCaches, shStagings;
	ID<Image> transLUT = {}, multiScattLUT = {};
	ID<Image> cameraVolume = {}, skyViewLUT = {};
	ID<Buffer> specularCache = {};
	ID<Framebuffer> transLutFramebuffer = {};
	ID<Framebuffer> skyViewLutFramebuffer = {};
	ID<GraphicsPipeline> transLutPipeline = {};
	ID<ComputePipeline> multiScattLutPipeline = {};
	ID<ComputePipeline> cameraVolumePipeline = {};
	ID<GraphicsPipeline> skyViewLutPipeline = {};
	ID<GraphicsPipeline> hdrSkyPipeline = {};
	ID<GraphicsPipeline> skyboxPipeline = {};
	ID<ComputePipeline> shGeneratePipeline = {};
	ID<ComputePipeline> shReducePipeline = {};
	ID<DescriptorSet> multiScattLutDS = {}, cameraVolumeDS = {};
	ID<DescriptorSet> skyViewLutDS = {}, hdrSkyDS = {}, skyboxDS = {};
	ID<DescriptorSet> shGenerateDS = {}, shReduceDS = {};
	ID<ImageView> skyboxViews[Image::cubemapFaceCount] = {};
	ID<Framebuffer> skyboxFramebuffers[Image::cubemapFaceCount] = {};
	Ref<Image> lastSkybox = {}, lastSpecular = {};
	ID<ImageView> lastSkyboxShView = {};
	uint32 shInFlightIndex = 0;
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
	void renderSkyboxFaces();
	void generateSkyShDiffuse(ID<Buffer> shDiffuse, f32x4x4* shCoeffs);

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
	float4 starColor = float4(float3(1.0f), 10000.0f);
	float starAngularSize = earthSunAngularSize; /**< (degrees) */
	float giFactor = 1.0f;        /**< Global illumination factor. */
	float multiScattFactor = 1.0f; /**< Light multi-scattering factor. */
	bool noDelay = false;         /**< Make all computation in one fram. (Expnesive!) */

	/*******************************************************************************************************************
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
	ID<Image> getMultiScattLUT();
	/**
	 * @brief Returns atmosphere camera volume scattering LUT. (Look Up Table)
	 */
	ID<Image> getCameraVolume();
	/**
	 * @brief Returns atmosphere sky view LUT. (Look Up Table)
	 */
	ID<Image> getSkyViewLUT();
	/**
	 * @brief Returns atmosphere specular cache buffer.
	 */
	ID<Buffer> getSpecularCache() const noexcept { return specularCache; }

	/**
	 * @brief Returns atmosphere transmittance LUT framebuffer.
	 */
	ID<Framebuffer> getTransLutFramebuffer();
	/**
	 * @brief Returns atmosphere sky view LUT framebuffer.
	 */
	ID<Framebuffer> getSkyViewLutFramebuffer();
	/**
	 * @brief Returns atmosphere skybox framebuffers.
	 */
	const ID<Framebuffer>* getSkyboxFramebuffers();

	/*******************************************************************************************************************
	 * @brief Returns atmosphere transmittance LUT graphics pipeline.
	 */
	ID<GraphicsPipeline> getTransLutPipeline();
	/**
	 * @brief Returns atmosphere multi-scattering LUT compute pipeline.
	 */
	ID<ComputePipeline> getMultiScattLutPipeline();
	/**
	 * @brief Returns atmosphere camera volume compute pipeline.
	 */
	ID<ComputePipeline> getCameraVolumePipeline();
	/**
	 * @brief Returns atmosphere sky view LUT graphics pipeline.
	 */
	ID<GraphicsPipeline> getSkyViewLutPipeline();
	/**
	 * @brief Returns atmosphere HDR sky graphics pipeline.
	 */
	ID<GraphicsPipeline> getHdrSkyPipeline();
	/**
	 * @brief Returns atmosphere skybox graphics pipeline.
	 */
	ID<GraphicsPipeline> getSkyboxPipeline();
	/**
	 * @brief Returns spherical harmonics generate compute pipeline.
	 */
	ID<ComputePipeline> getShGeneratePipeline();
	/**
	 * @brief Returns spherical harmonics reduce compute pipeline.
	 */
	ID<ComputePipeline> getShReducePipeline();

	/**
	 * @brief Returns camera volume slice constants at the specified graphics quality.
	 *
	 * @param quality target graphics quality level
	 * @param[out] sliceCount camera volume slice count
	 * @param[out] kmPerSlice camera volume kilometres per slice
	 */
	static void getSliceQuality(GraphicsQuality quality, float& sliceCount, float& kmPerSlice) noexcept;
};

} // namespace garden
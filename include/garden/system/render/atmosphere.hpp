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
 * One of implementations: https://www.shadertoy.com/view/slSXRW
 * 
 * Measurement Units:
 *   1 megametre(Mm) = 1000 kilometre(km)
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
	/**
	 * brief Atmosphere gas types.
	 */
	enum class Gas : uint8
	{
		He, Ne, Ar, Kr, Xe, H2, N2, O2, CH4, CO, CO2, Count
	};

	struct TransmittancePC final
	{
		float3 rayleighScattering;
		float rayDensityExpScale;
		float3 mieExtinction;
		float mieDensityExpScale;
		float3 absorptionExtinction;
		float miePhaseG;
		float3 sunDir;
		float absDensity0LayerWidth;
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
		float3 groundAlbedo;
		float absDensity0ConstantTerm;
		float3 sunDir;
		float absDensity0LinearTerm;
		float3 cameraPos;
		float absDensity1ConstantTerm;
		float absDensity1LinearTerm;
		float bottomRadius;
		float topRadius;
	};

	// Note: red is higher than peak 580 nm.
	static constexpr float wavelengthR = 680.0f; /**< Peak red sensitivity of human vision. (nm) */
	static constexpr float wavelengthG = 550.0f; /**< Peak green sensitivity of human vision. (nm) */
	static constexpr float wavelengthB = 440.0f; /**< Peak blue sensitivity of human vision. (nm) */

	static constexpr float earthRadius = 6371.0f;      /**< Earth volumetric mean radius. (km) */
	static constexpr float earthKarmanLine = 100.0f;   /**< Earth atmosphere height. (km) */
	static constexpr float earthAirIOR = 1.0003f;      /**< Earth air index of refraction. (Approx 1.000293) */
	static constexpr float earthAirDensity = 1.15465f; /**< Earth air density. (kg/m^3 at 30C and 1atm) */
	static constexpr float earthBondAlbedo = 0.3f;     /**< Earth bond albedo factor. */

	static constexpr float marsRadius = 3389.0f;   /**< Earth volumetric mean radius. (km) */
	static constexpr float marsKarmanLine = 80.0f; /**< Earth atmosphere height. (km) */
	static constexpr float marsAirIOR = 1.00028f;  /**< Earth air index of refraction. (Approx) */
	static constexpr float marsAirDensity = 0.02f; /**< Earth air density. (kg/m^3 at 30C and 1atm) */
	static constexpr float marsBondAlbedo = 0.25f; /**< Earth bond albedo factor. */

	static constexpr uint2 transLutSize = uint2(256, 64);
	static constexpr uint32 multiScatLutLength = 32;
private:
	ID<Image> transLUT = {};
	ID<Image> multiScatLUT = {};
	ID<Image> skyViewLUT = {};
	ID<Framebuffer> transLutFramebuffer = {};
	ID<Framebuffer> skyViewFramebuffer = {};
	ID<GraphicsPipeline> transLutPipeline = {};
	ID<ComputePipeline> multiScatPipeline = {};
	ID<GraphicsPipeline> skyViewPipeline = {};
	ID<ComputePipeline> volumesPipeline = {};
	ID<DescriptorSet> multiScatDS = {};
	ID<DescriptorSet> skyViewDS = {};
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
	void render();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;
	float4 rayleighScattering = float4(0.005802f, 0.013558f, 0.033100f, 1);
	f32x4 ozoneAbsorption = f32x4(0.65f, 1.881f, 0.085f);
	float planetRadius = earthRadius;
	float atmosphereHeight = earthKarmanLine;
	float rayleighAbsorption = 0.0f;
	float mieScattering = 3.996f;
	float mieAbsorption = 4.4f;
	float ozoneScattering = 0.0f;
};

//**********************************************************************************************************************
static constexpr float gasMolarMasses[(psize)AtmosphereRenderSystem::Gas::Count] =
{
	4.002602f, 20.1797f, 39.948f, 83.798f, 131.293f, 2.01588f,
	28.0134f, 31.9988f, 16.0425f, 28.0101f, 44.0095f,
};

static float gasToMolarMass(AtmosphereRenderSystem::Gas gas)
{
	GARDEN_ASSERT((psize)gas < (psize)AtmosphereRenderSystem::Gas::Count);
	return gasMolarMasses[(psize)gas];
}

static float calcMolarMass(const vector<pair<float, AtmosphereRenderSystem::Gas>>& gases) noexcept // g/mol
{
	double molarMass = 0.0;
	for (auto pair : gases)
		molarMass += (double)gasToMolarMass(pair.second) * pair.first;
	return (float)molarMass;
}
static constexpr double calcMolecularDensity(float density, float molarMass) noexcept
{
	constexpr auto avogadro = 6.02214076e+23;
	return ((double)density / (double)molarMass) * avogadro;
}
static constexpr float calcRayleighScattering(float wavelength,
	float airIOR, double molecularDensity) noexcept
{
	auto w = (double)wavelength * 1e-9;
	auto m = molecularDensity * 1e+3;
	auto x = ((double)airIOR * (double)airIOR - 1.0);
	auto n = 8.0 * (M_PI * M_PI * M_PI) * (x * x);
	auto d = 3.0 * m * (w * w * w * w);
	return (float)(n / d);
}
static constexpr float3 calcRayleighScattering(float airIOR, double molecularDensity) noexcept
{
	// TODO: vectorize
	return float3(
		calcRayleighScattering(AtmosphereRenderSystem::wavelengthR, airIOR, molecularDensity),
		calcRayleighScattering(AtmosphereRenderSystem::wavelengthG, airIOR, molecularDensity),
		calcRayleighScattering(AtmosphereRenderSystem::wavelengthB, airIOR, molecularDensity)); 
}

//**********************************************************************************************************************
static float calcEarthAirMolarMass()
{
	static const vector<pair<float, AtmosphereRenderSystem::Gas>> gasses =
	{
		{ 0.78084f, AtmosphereRenderSystem::Gas::N2 }, 
		{ 0.20946f, AtmosphereRenderSystem::Gas::O2 }, 
		{ 0.00934f, AtmosphereRenderSystem::Gas::Ar }, 
		{ 0.00033f, AtmosphereRenderSystem::Gas::CO2 }, 
		{ 0.00001818f, AtmosphereRenderSystem::Gas::Ne }, 
		{ 0.00000524f, AtmosphereRenderSystem::Gas::He }, 
		{ 0.00000179f, AtmosphereRenderSystem::Gas::CH4 }, 
		{ 0.000001f, AtmosphereRenderSystem::Gas::Kr }, 
		{ 0.0000005f, AtmosphereRenderSystem::Gas::H2 }, 
		{ 0.00000009f, AtmosphereRenderSystem::Gas::Xe }
	};
	return calcMolarMass(gasses);
}
static float calcMarsAirMolarMass()
{
	static const vector<pair<float, AtmosphereRenderSystem::Gas>> gasses =
	{
		{ 0.9532f, AtmosphereRenderSystem::Gas::CO2 }, 
		{ 0.027f, AtmosphereRenderSystem::Gas::N2 }, 
		{ 0.016f, AtmosphereRenderSystem::Gas::Ar }, 
		{ 0.0013f, AtmosphereRenderSystem::Gas::O2 }, 
		{ 0.0007f, AtmosphereRenderSystem::Gas::CO }, 
		{ 0.0000025f, AtmosphereRenderSystem::Gas::Ne }, 
		{ 0.0000003f, AtmosphereRenderSystem::Gas::Kr },
		{ 0.00000008f, AtmosphereRenderSystem::Gas::Xe }
	};
	return calcMolarMass(gasses);
}

} // namespace garden
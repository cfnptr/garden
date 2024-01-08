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
// Based on this paper: https://sebh.github.io/publications/egsr2020.pdf
// One of implementations: https://www.shadertoy.com/view/slSXRW
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/system/graphics.hpp"

// Measurement Units
// 1 megametre(Mm) = 1000 kilometre(km)

// Peak Sensitivity of Human Vision (nm)
#define WAVELENGTH_R 680 // Higher than peak 580
#define WAVELENGTH_G 550
#define WAVELENGTH_B 440

#define EARTH_RADIUS 6371.0 // km - volumetric mean radius
#define EARTH_ATMOSPHERE_HEIGHT 100.0 // km - karman line
#define EARTH_AIR_IOR 1.0003 // approx 1.000293
#define EARTH_AIR_DENSITY 1.15465 // kg/m^3 at 30C and 1atm
#define EARTH_BOND_ALBEDO 0.3

#define MARS_RADIUS 3389.0 // km - volumetric mean radius
#define MARS_ATMOSPHERE_HEIGHT 80.0 // km - karman line
#define MARS_AIR_IOR 1.00028 // approx little less than earth
#define MARS_AIR_DENSITY 0.020
#define MARS_BOND_ALBEDO 0.25

namespace garden
{

using namespace garden;
using namespace garden::graphics;

enum class Gas : uint8
{
	He, Ne, Ar, Kr, Xe, H2, N2, O2, CH4, CO, CO2, Count
};

//--------------------------------------------------------------------------------------------------
// Physically Based Atmosphere
class AtmosphereRenderSystem final : public System, public IRenderSystem
{
	void initialize() final;	
	void render() final;
	
	friend class ecsm::Manager;
public:
	float planetRadius = EARTH_RADIUS;
	float atmosphereHeight = EARTH_ATMOSPHERE_HEIGHT;
	float3 rayleighScattering = float3(5.802f, 13.558f, 33.1f);
	float rayleighAbsorption = 0.0f;
	float mieScattering = 3.996f;
	float mieAbsorption = 4.4f;
	float ozoneScattering = 0.0f;
	float3 ozoneAbsorption = float3(0.65f, 1.881f, 0.085f);
};

//--------------------------------------------------------------------------------------------------
static const float gasMolarMasses[(psize)Gas::Count] =
{
	4.002602f, 20.1797f, 39.948f, 83.798f, 131.293f, 2.01588f,
	28.0134f, 31.9988f, 16.0425f, 28.0101f, 44.0095f,
};

static float gasToMolarMass(Gas gas)
{
	GARDEN_ASSERT((psize)gas < (psize)Gas::Count)
	return gasMolarMasses[(psize)gas];
}

//--------------------------------------------------------------------------------------------------
static float calcMolarMass(const vector<pair<float, Gas>>& gases) noexcept // g/mol
{
	double molarMass = 0.0;
	for (auto pair : gases)
		molarMass += (double)gasToMolarMass(pair.second) * pair.first;
	return (float)molarMass;
}
static double calcMolecularDensity(float density, float molarMass) noexcept
{
	const auto avogadro = 6.02214076e+23;
	return ((double)density / (double)molarMass) * avogadro;
}
static float calcRayleighScattering(float wavelength,
	float airIOR, double molecularDensity) noexcept
{
	auto w = (double)wavelength * 1e-9;
	auto m = molecularDensity * 1e+3;
	auto x = ((double)airIOR * (double)airIOR - 1.0);
	auto n = 8.0 * (M_PI * M_PI * M_PI) * (x * x);
	auto d = 3.0 * m * (w * w * w * w);
	return (float)(n / d);
}
static float3 calcRayleighScattering(float airIOR, double molecularDensity) noexcept
{
	return float3(
		calcRayleighScattering(WAVELENGTH_R, airIOR, molecularDensity),
		calcRayleighScattering(WAVELENGTH_G, airIOR, molecularDensity),
		calcRayleighScattering(WAVELENGTH_B, airIOR, molecularDensity));
}

//--------------------------------------------------------------------------------------------------
static float calcEarthAirMolarMass()
{
	const vector<pair<float, Gas>> gasses =
	{
		{ 0.78084f, Gas::N2 }, { 0.20946f, Gas::O2 }, { 0.00934f, Gas::Ar },
		{ 0.00033f, Gas::CO2 }, { 0.00001818f, Gas::Ne }, { 0.00000524f, Gas::He },
		{ 0.00000179f, Gas::CH4 },  { 0.000001f, Gas::Kr },
		{ 0.0000005f, Gas::H2 }, { 0.00000009f, Gas::Xe }
	};
	return calcMolarMass(gasses);
}
static float calcMarsAirMolarMass()
{
	const vector<pair<float, Gas>> gasses =
	{
		{ 0.9532f, Gas::CO2 }, { 0.027f, Gas::N2 }, { 0.016f, Gas::Ar },
		{ 0.0013f, Gas::O2 }, { 0.0007f, Gas::CO }, { 0.0000025f, Gas::Ne },
		{ 0.0000003f, Gas::Kr }, { 0.00000008f, Gas::Xe }
	};
	return calcMolarMass(gasses);
}

} // garden
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
 * @brief Nvidia DLSS rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

#if GARDEN_NVIDIA_DLSS

namespace garden
{

/**
 * @brief Nvidia DLSS performance quality modes.
 */
enum class DlssQuality : uint8
{
	Off, UltraPerformance, Performance, Balanced, Quality, UltraQuality, DLAA, Count
};

/**
 * @brief Nvidia DLSS rendering system. (Deep Learning Super Sampling)
 */
class DlssRenderSystem final : public System, public Singleton<DlssRenderSystem>
{
	void* parameters = nullptr;
	void* feature = nullptr;
	uint2 optimalSize = uint2::zero;
	uint2 minSize = uint2::zero;
	uint2 maxSize = uint2::zero;
	float sharpness = 0.0f;
	DlssQuality quality = DlssQuality::Balanced;

	/**
	 * @brief Creates a new Nvidia DLSS rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	DlssRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys Nvidia DLSS rendering system instance.
	 */
	~DlssRenderSystem() final;

	void preInit();
	void postDeinit();
	void preLdrRender();
	void swapchainRecreate();
	
	static void createDlssFeatureCommand(void* commandBuffer, void* argument);
	static void evaluateDlssCommand(void* commandBuffer, void* argument);

	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns Nvidia DLSS quality mode.
	 */
	DlssQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Sets Nvidia DLSS quality mode.
	 * @param quality target DLSS quality
	 */
	void setQuality(DlssQuality quality);

	/**
	 * @brief Calculates upscaled mip-map LOD bias.
	 * @param nativeBias native mip LOD bias
	 */
	float calcMipLodBias(float nativeBias = 0.0f) noexcept;
};

/**
 * @brief Nvidia DLSS performance quality name strings.
 */
constexpr const char* dlssQualityNames[(psize)DlssQuality::Count] =
{
	"Off", "UltraPerformance", "Performance", "Balanced", "Quality", "UltraQuality", "DLAA"
};
/**
 * @brief Returns Nvidia DLSS performance quality mode.
 * @param dlssQuality target DLSS quality mode
 * @throw GardenError on unknown DLSS quality mode.
 */
static DlssQuality toDlssQuality(string_view dlssQuality)
{
	if (dlssQuality == "Off") return DlssQuality::Off;
	if (dlssQuality == "UltraPerformance") return DlssQuality::UltraPerformance;
	if (dlssQuality == "Performance") return DlssQuality::Performance;
	if (dlssQuality == "Balanced") return DlssQuality::Balanced;
	if (dlssQuality == "Quality") return DlssQuality::Quality;
	if (dlssQuality == "UltraQuality") return DlssQuality::UltraQuality;
	if (dlssQuality == "DLAA") return DlssQuality::DLAA;
	throw GardenError("Unknown DLSS quality. (" + string(dlssQuality) + ")");
}
/**
 * @brief Returns Nvidia DLSS performance quality name string.
 * @param dlssQuality target DLSS quality mode
 */
static string_view toString(DlssQuality dlssQuality) noexcept
{
	GARDEN_ASSERT(dlssQuality < DlssQuality::Count);
	return dlssQualityNames[(psize)dlssQuality];
}

} // namespace garden

#endif // GARDEN_NVIDIA_DLSS
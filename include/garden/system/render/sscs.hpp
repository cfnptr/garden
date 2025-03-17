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
 * @brief Screen space contact shadows rendering functions.
 * @details Based on this: https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Screen space contact shadows rendering system. (SSS)
 * 
 * @details
 * A post-processing technique that enhances realism by simulating the small, subtle shadows that appear where objects 
 * meet surfaces. Instead of relying on full 3D shadow maps for these detailed areas, the system uses information 
 * already available in the screen's depth buffer to calculate and render these shadows directly in screen space.
 */
class SscsRenderSystem final : public System, public Singleton<SscsRenderSystem>
{
public:
	/**
	 * @brief Screen space contact shadows properties container.
	 * 
	 * @details
	 * If a very flat surface is being lit and rendered at an grazing angles, the edge detect may incorrectly detect 
	 * multiple 'edge' pixels along that flat surface. In these cases, the grazing angle of the light may subsequently 
	 * produce aliasing artifacts in the shadow where these incorrect edges were detected. Setting 'ignoreEdgePixels' 
	 * value to true would mean that those pixels would not cast a shadow, however it can also thin out otherwise valid 
	 * shadows, especially on foliage edges.
	 * 
	 * There are two 'bilinearSamplingOffsetMode' modes to compute bilinear samples for shadow depth:
	 *   true  = sampling points for pixels are offset to the wavefront shared ray, shadow depths and starting depths are 
	 *           the same. Can project more jagged / aliased shadow lines in some cases.
	 *   false = sampling points for pixels are not offset and start from pixel centers. Shadow depths are biased based 
	 *           on depth gradient across the current pixel bilinear sample. Has more issues in back-face / grazing areas.
	 * Both modes have subtle visual differences, which (may / may not) exaggerate depth buffer aliasing that gets 
	 * projected in to the shadow. Evaluating the visual difference between each mode is recommended.
	 */
	struct Properties final
	{
		float2 depthBounds = float2(0.0f, 1.0f); /**< Bounds for the on-screen volume of the light. */ 
		uint32 hardShadowSamples = 1;            /**< Number of initial shadow samples that will produce a hard shadow. */
		uint32 fadeOutSamples = 64;               /**< Number of samples that will fade out at the end of the shadow. (minor cost) */
		float surfaceThickness = 0.01f;         /**< Assumed thickness of each pixel for shadow-casting. */
		float bilinearThreshold = 0.02f;         /**< Threshold for determining if depth difference represents an edge. */
		float shadowContrast = 4.0f;             /**< Contrast boost applied to the transition in/out of the shadow. (>= 1) */
		bool ignoreEdgePixels : 1;               /**< If an edge is detected, pixel will not contribute to the shadow. */
		bool usePrecisionOffset : 1;             /**< Small offset is applied to account for an imprecise depth buffer. */
		bool bilinearSamplingOffsetMode : 1;     /**< Target mode to compute bilinear samples for shadow depth. */
		bool useEarlyOut : 1;                    /**< Early-out when depth values are not within 'depthBounds'. */
		bool debugOutputEdgeMask : 1;            /**< Visualize edges, for tuning the 'bilinearThreshold' value. */
		bool debugOutputThreadIndex : 1;         /**< Visualize layout of compute threads. */
		bool debugOutputWaveIndex : 1;           /**< Visualize layout of compute wavefronts. */
	private:
		uint8 _unused : 1;
	public:
		/**
		 * @brief Creates a new SSCS system default properties container.
		 */
		constexpr Properties() noexcept : ignoreEdgePixels(false), usePrecisionOffset(false), 
			bilinearSamplingOffsetMode(false), debugOutputEdgeMask(false), debugOutputThreadIndex(false), 
			debugOutputWaveIndex(false), useEarlyOut(false), _unused(0) { }
	};
	struct PushConstants final
	{
		float4 lightCoordinate;
		float2 invDepthTexSize;
		int2 waveOffset;
	};
private:
	ID<ComputePipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	Properties properties = {};

	/**
	 * @brief Creates a new screen space contact shadows rendering system instance. (SSS)
	 * @param setSingleton set system singleton instance
	 */
	SscsRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys screen space contact shadows rendering system instance. (SS)
	 */
	~SscsRenderSystem() final;

	void init();
	void deinit();
	void postShadowRender();
	void shadowRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;

	/**
	 * @brief Returns screen space contact shadows compute pipeline.
	 */
	ID<ComputePipeline> getPipeline();
};

} // namespace garden
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
 * @brief GPU data processing rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief GPU data proccessing system.
 */
class GpuProcessSystem final : public System, public Singleton<GpuProcessSystem>
{
public:
	struct BoxBlurPC final
	{
		float2 texelSize;
	};
	struct GaussianBlurPC final
	{
		float2 texelSize;
		float intensity;
	};
	struct BilateralBlurPC final
	{
		float2 texelSize;
		float nearPlane;
		float sharpness;
	};
private:
	ID<Buffer> ggxBlurKernel = {};
	ID<ComputePipeline> downsampleNormPipeline = {};
	ID<ComputePipeline> downsampleNormAPipeline = {};

	/**
	 * @brief Creates a new GPU data processing system instance.
	 * @param setSingleton set system singleton instance
	 */
	GpuProcessSystem(bool setSingleton = true);
	/**
	 * @brief Destroys GPU data processing system instance.
	 */
	~GpuProcessSystem() final;

	void deinit();
	friend class ecsm::Manager;
public:
	/**
	 * @brief Returns spherical GGX distribution blur kernel.
	 */
	ID<Buffer> getGgxBlurKernel();
	/**
	 * @brief Returns GPU process downsample normals pipeline.
	 */
	ID<ComputePipeline> getDownsampleNorm();
	/**
	 * @brief Returns GPU process downsample normals array pipeline.
	 */
	ID<ComputePipeline> getDownsampleNormA();

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Records image mips generation command.
	 *
	 * @param image target image to generate mips for
	 * @param pipeline mip generation compute pipeline
	 */
	void generateMips(ID<Image> image, ID<ComputePipeline> pipeline);
	/**
	 * @brief Records normal map mips generation command.
	 * @param normalMap target normal map image
	 */
	void normalMapMips(ID<Image> normalMap);

	/**
	 * @brief Calculates gaussian blur kernel coefficients.
	 *
	 * @param sigma ammount of bluring
	 * @param[out] target gaussian kernel coefficients
	 * @param coeffCount coefficients buffer size
	 */
	static void calcGaussCoeffs(float sigma, float2* coeffs, uint8 coeffCount) noexcept;

	/**
	 * @brief Records gaussian blur command.
	 *
	 * @param srcBuffer source data buffer
	 * @param dstFramebuffer destination framebuffer
	 * @param tmpFramebuffer temporary framebuffer
	 * @param kernelBuffer blur coefficients buffer
	 * @param reinhard use reinhard weighted filter
	 * @param[in,out] pipeline gaussian blur graphics pipeline
	 * @param[in,out] descriptorSet gaussian blur descriptor set
	 */
	void gaussianBlur(ID<ImageView> srcBuffer, ID<Framebuffer> dstFramebuffer, 
		ID<Framebuffer> tmpFramebuffer, ID<Buffer> kernelBuffer, float intensity,
		bool reinhard, ID<GraphicsPipeline>& pipeline, ID<DescriptorSet>& descriptorSet);

	/**
	 * @brief Records bilateral blur command. (Depth aware)
	 *
	 * @param srcBuffer source data buffer
	 * @param dstFramebuffer destination framebuffer
	 * @param tmpFramebuffer temporary framebuffer
	 * @param sharpness blur sharpness
	 * @param[in,out] pipeline bilateral blur graphics pipeline
	 * @param[in,out] descriptorSet bilateral blur descriptor set
	 * @param kernelRadius radius of the blur kernel
	 */
	void bilateralBlurD(ID<ImageView> srcBuffer, ID<Framebuffer> dstFramebuffer,  ID<Framebuffer> tmpFramebuffer, 
		float sharpness, ID<GraphicsPipeline>& pipeline, ID<DescriptorSet>& descriptorSet, uint8 kernelRadius = 3);

	/**
	 * @brief Prepares spherical GGX distribution blur data.
	 * 
	 * @param buffer target blur buffer
	 * @param[in,out] imageViews GGX blur image views
	 * @param[in,out] framebuffers GGX blur framebuffers
	 */
	void prepareGgxBlur(ID<Image> buffer, vector<ID<ImageView>>& imageViews, vector<ID<Framebuffer>>& framebuffers);
	/**
	 * @brief Records spherical GGX distribution blur command.
	 * @return True if all resources are ready and blur command has been recorder.
	 * 
	 * @param buffer target blur buffer
	 * @param[in] imageViews GGX blur image views
	 * @param[in] framebuffers GGX blur framebuffers
	 * @param[in,out] pipeline GGX blur graphics pipeline
	 * @param[in,out] descriptorSets GGX blur descriptor sets
	 */
	bool ggxBlur(ID<Image> buffer, const vector<ID<ImageView>>& imageViews, const vector<ID<Framebuffer>>& framebuffers, 
		ID<GraphicsPipeline>& pipeline, vector<ID<DescriptorSet>>& descriptorSets);

	static const uint8 ggxCoeffCount; /** GGX kernel coefficient count. */
};

} // namespace garden
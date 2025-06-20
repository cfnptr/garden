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
	struct BilateralBlurPC final
	{
		float2 texelSize;
		float nearPlane;
		float sharpness;
	};
private:
	ID<ComputePipeline> downsampleNormPipeline = {};
	ID<ComputePipeline> downsampleNormAPipeline = {};
	ID<GraphicsPipeline> boxBlurPipeline = {};
	ID<GraphicsPipeline> bilatBlurDPipeline = {};

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
	 * @brief Returns GPU process downsample normals pipeline.
	 */
	ID<ComputePipeline> getDownsampleNorm();
	/**
	 * @brief Returns GPU process downsample normals array pipeline.
	 */
	ID<ComputePipeline> getDownsampleNormA();
	/**
	 * @brief Returns GPU process box blur pipeline.
	 */
	ID<GraphicsPipeline> getBoxBlur();
	/**
	 * @brief Returns GPU process bilateral blur pipeline. (Depth aware)
	 */
	ID<GraphicsPipeline> getBilateralBlurD();

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
	 * @brief Records bilateral blur command. (Depth aware)
	 *
	 * @param srcBuffer source data buffer
	 * @param dstFramebuffer destination framebuffer
	 * @param tmpBuffer temporary data buffer
	 * @param tmpFramebuffer temporary framebuffer
	 * @param scale texel size scale
	 * @param sharpness blur sharpness
	 * @param[in,out] descriptorSet blur descriptor set
	 */
	void bilateralBlurD(ID<ImageView> srcBuffer, ID<Framebuffer> dstFramebuffer,
		ID<ImageView> tmpBuffer, ID<Framebuffer> tmpFramebuffer,
		float2 scale, float sharpness, ID<DescriptorSet>& descriptorSet);
};

} // namespace garden
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
 * @brief Physically based volumetric clouds rendering functions.
 * @details Based on this: https://www.researchgate.net/publication/345694869_Physically_Based_Sky_Atmosphere_Cloud_Rendering_in_Frostbite
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Physically based volumetric clouds rendering system.
 */
class CloudsRenderSystem final : public System, public Singleton<CloudsRenderSystem>
{
public:
	struct PushConstants final
	{
		float4x4 invViewProj;
		float3 cameraPos;
		float bottomRadius;
		float topRadius;
		float minDistance;
		float maxDistance;
	};

	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	Ref<Image> noiseShape = {}, noiseErosion = {};
	ID<Image> cloudsView = {}, cloudsCube = {};
	ID<Framebuffer> viewFramebuffer = {};
	ID<Framebuffer> cubeFramebuffer = {};
	ID<GraphicsPipeline> cloudsPipeline = {};
	ID<GraphicsPipeline> blendPipeline = {};
	ID<DescriptorSet> cloudsDS = {}, viewBlendDS = {}, cubeBlendDS;
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;
	uint8 _alignment = 0;

	/**
	 * @brief Creates a new physically based volumetric clouds rendering system instance. (FXAA)
	 * @param setSingleton set system singleton instance
	 */
	CloudsRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys physically based volumetric clouds rendering system instance. (FXAA)
	 */
	~CloudsRenderSystem() final;

	void init();
	void deinit();
	void preDeferredRender();
	void preHdrRender();
	void hdrRender();
	void gBufferRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;     /**< Is physically based volumetric clouds rendering enabled. */
	float bottomRadius = 1.5f; /**< Stratus and cumulus clouds start height. (km) */
	float topRadius = 4.0f;    /**< Stratus and cumulus clouds end height. (km) */
	float minDistance = 0.01f; /**< Clouds volume offset in front of camera. (km) */
	float maxDistance = 35.0f; /**< Maximum clouds volume tracing distance. (km) */

	/**
	 * @brief Returns physically based volumetric clouds noise shape image.
	 */
	Ref<Image> getNoiseShape();
	/**
	 * @brief Returns physically based volumetric clouds noise erosion image.
	 */
	Ref<Image> getNoiseErosion();
	/**
	 * @brief Returns physically based volumetric clouds view image.
	 */
	ID<Image> getCloudsView();
	/**
	 * @brief Returns physically based volumetric clouds view framebuffer.
	 */
	ID<Framebuffer> getViewFramebuffer();
	/**
	 * @brief Returns physically based volumetric clouds graphics pipeline.
	 */
	ID<GraphicsPipeline> getCloudsPipeline();
	/**
	 * @brief Returns physically based volumetric clouds blend graphics pipeline.
	 */
	ID<GraphicsPipeline> getBlendPipeline();
};

} // namespace garden
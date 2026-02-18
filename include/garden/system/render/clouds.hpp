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
 * @brief Physically based volumetric clouds rendering functions.
 *
 * @details
 * Based on these: 
 * https://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf
 * https://advances.realtimerendering.com/s2017/Nubis%20-%20Authoring%20Realtime%20Volumetric%20Cloudscapes%20with%20the%20Decima%20Engine%20-%20Final%20.pdf
 * https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf
 * https://advances.realtimerendering.com/s2023/Nubis%20Cubed%20(Advances%202023).pdf
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Physically based volumetric clouds rendering system.
 *
 * @details
 * Physically based volumetric cloud rendering is a technique that simulates the interaction of light with 3D density 
 * fields to create realistic skybox and atmospheric effects. Unlike traditional skyboxes that use 2D textures, 
 * this system utilizes ray marching to traverse a volume to calculate how light is absorbed and scattered 
 * within the medium. By applying the Volume Rendering Equation, the engine can simulate complex optical phenomena 
 * like multiple scattering (which gives clouds their soft, luminous look), Beer’s Law for light attenuation, and 
 * anisotropic scattering (the silver-lining effect). This allows for dynamic, high-fidelity clouds that interact 
 * naturally with time-of-day systems, cast real-time shadows on the terrain, and support seamless camera transitions 
 * from the ground through the cloud layer into space.
 */
class CloudsRenderSystem final : public System, public Singleton<CloudsRenderSystem>
{
public:
	struct CamViewPC final
	{
		float3 cameraPos;
		float groundRadius;
		uint2 bayerPos;
		float atmTopRadius;
		float bottomRadius;
		float topRadius;
		float minDistance;
		float maxDistance;
		float currentTime;
		float cumulusCoverage;
		float cirrusCoverage;
		float temperatureDiff;
	};
	struct SkyboxPC final
	{
		float4x4 invViewProj;
		float3 cameraPos;
		float groundRadius;
		float atmTopRadius;
		float bottomRadius;
		float topRadius;
		float minDistance;
		float maxDistance;
		float currentTime;
		float cumulusCoverage;
		float cirrusCoverage;
		float temperatureDiff;
	};
	struct ShadowsPC final
	{
		float4x4 invViewProj;
		float3 cameraPos;
		float bottomRadius;
		float3 starDir;
		float currentTime;
		float3 windDir;
		float cumulusCoverage;
		float temperatureDiff;
	};

	static constexpr Image::Format cloudsColorFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format cloudsDepthFormat = Image::Format::SfloatR16;
	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	Ref<Image> dataFields = {}, vertProfile = {}, noiseShape = {}, cirrusShape = {};
	ID<Image> cloudsCamView = {}, cloudsCamViewDepth = {}, cloudsSkybox = {};
	ID<Framebuffer> camViewFramebuffer = {}, skyboxFramebuffer = {};
	ID<GraphicsPipeline> camViewPipeline = {}, skyboxPipeline = {}, 
		viewBlendPipeline = {}, skyBlendPipeline = {}, shadowPipeline = {};
	ID<DescriptorSet> camViewDS = {}, skyboxDS = {}, viewBlendDS = {}, skyBlendDS = {}, shadowDS = {};
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false, hasShadows = false;

	/*******************************************************************************************************************
	 * @brief Creates a new physically based volumetric clouds rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	CloudsRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys physically based volumetric clouds rendering system instance.
	 */
	~CloudsRenderSystem() final;

	void init();
	void deinit();
	void preDeferredRender();
	void preSkyFaceRender();
	void skyFaceRender();
	void preHdrRender();
	void hdrRender();
	void preShadowRender();
	void shadowRender();
	void gBufferRecreate();
	void qualityChange();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;        /**< Is physically based volumetric clouds rendering enabled. */
	bool renderShadows = true;    /**< Render cloud shadows to the shadow buffer. */
	float bottomRadius = 1.5f;    /**< Stratus and cumulus clouds start height. (km) */
	float topRadius = 4.0f;       /**< Stratus and cumulus clouds end height. (km) */
	float minDistance = 0.2f;     /**< Clouds volume tracing offset in front of camera. (km) */
	float maxDistance = 200.0f;   /**< Maximum clouds volume tracing distance. (km) */
	float cumulusCoverage = 0.4f; /**< Ammount of cumulus clouds. (Clear or cloudy weather) */
	float cirrusCoverage = 0.2f;  /**< Ammount of cirrus clouds. (Clear or cloudy weather) */
	float temperatureDiff = 0.7f; /**< Temperature difference between layers. (Storm clouds) */
	float currentTime = 0.0f;     /**< Custom current time value. (For a multiplayer sync) */
	bool noDelay = false;         /**< Make all computation in one fram. (Expnesive!) */

	/**
	 * @brief Returns volumetric clouds rendering graphics quality.
	 */
	GraphicsQuality getQuality() const noexcept { return quality; }
	/**
	 * @brief Sets volumetric clouds rendering graphics quality.
	 * @param quality target graphics quality level
	 */
	void setQuality(GraphicsQuality quality);

	/**
	 * @brief Returns volumetric clouds data fields image.
	 */
	Ref<Image> getDataFields();
	/**
	 * @brief Returns volumetric clouds vertical profile image.
	 */
	Ref<Image> getVertProfile();
	/**
	 * @brief Returns volumetric clouds noise shape image.
	 */
	Ref<Image> getNoiseShape();
	/**
	 * @brief Returns volumetric clouds camera view image.
	 */
	ID<Image> getCloudsCamView();
	/**
	 * @brief Returns volumetric clouds camera view depth image.
	 */
	ID<Image> getCloudsCamViewDepth();
	/**
	 * @brief Returns volumetric clouds skybox image.
	 */
	ID<Image> getCloudsSkybox() const noexcept { return cloudsSkybox; }

	/**
	 * @brief Returns volumetric clouds camera view framebuffer.
	 */
	ID<Framebuffer> getCamViewFramebuffer();
	/**
	 * @brief Returns volumetric clouds camera view framebuffer.
	 */
	ID<Framebuffer> getSkyboxFramebuffer() const noexcept { return skyboxFramebuffer; }

	/**
	 * @brief Returns volumetric clouds camera view graphics pipeline.
	 */
	ID<GraphicsPipeline> getCamViewPipeline();
	/**
	 * @brief Returns volumetric clouds skybox graphics pipeline.
	 */
	ID<GraphicsPipeline> getSkyboxPipeline() const noexcept { return skyboxPipeline; }
	/**
	 * @brief Returns volumetric clouds camera view blend graphics pipeline.
	 */
	ID<GraphicsPipeline> getViewBlendPipeline();
	/**
	 * @brief Returns volumetric clouds shadow graphics pipeline.
	 */
	ID<GraphicsPipeline> getShadowPipeline();
};

} // namespace garden
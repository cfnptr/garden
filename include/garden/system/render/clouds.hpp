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
	struct PushConstants final
	{
		float3 cameraPos;
		float bottomRadius;
		float topRadius;
		float minDistance;
		float maxDistance;
		float currentTime;
		float coverage;
		float temperature;
	};

	static constexpr Image::Format cloudsColorFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format cloudsDepthFormat = Image::Format::SfloatR16;
	static constexpr Framebuffer::OutputAttachment::Flags framebufferFlags = { false, false, true };
private:
	Ref<Image> dataFields = {}, verticalProfile = {}, noiseShape = {};
	ID<Image> cloudsView = {}, cloudsViewDepth = {};
	ID<Image> cloudsCube = {}, lastCameraVolume = {};
	ID<Framebuffer> viewFramebuffer = {}, cubeFramebuffer = {};
	ID<GraphicsPipeline> computePipeline = {};
	ID<GraphicsPipeline> viewBlendPipeline = {};
	ID<DescriptorSet> computeDS = {}, viewBlendDS = {}, cubeBlendDS = {};
	GraphicsQuality quality = GraphicsQuality::High;
	bool isInitialized = false;
	uint8 _alignment = 0;

	/**
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
	void preHdrRender();
	void depthHdrRender();
	void gBufferRecreate();
	void qualityChange();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;      /**< Is physically based volumetric clouds rendering enabled. */
	float bottomRadius = 1.5f;  /**< Stratus and cumulus clouds start height. (km) */
	float topRadius = 4.0f;     /**< Stratus and cumulus clouds end height. (km) */
	float minDistance = 0.2f;   /**< Clouds volume tracing offset in front of camera. (km) */
	float maxDistance = 200.0f; /**< Maximum clouds volume tracing distance. (km) */
	float coverage = 0.4f;      /**< Ammount of clouds. (Clear or cloudy weather) */
	float temperature = 0.7f;   /**< Temperature difference between layers. (Storm clouds) */
	float currentTime = 0.0f;   /**< Custom current time value. (For a multiplayer sync) */

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
	Ref<Image> getVerticalProfile();
	/**
	 * @brief Returns volumetric clouds noise shape image.
	 */
	Ref<Image> getNoiseShape();
	/**
	 * @brief Returns volumetric clouds view image.
	 */
	ID<Image> getCloudsView();
	/**
	 * @brief Returns volumetric clouds view depth image.
	 */
	ID<Image> getCloudsViewDepth();

	/**
	 * @brief Returns volumetric clouds view framebuffer.
	 */
	ID<Framebuffer> getViewFramebuffer();

	/**
	 * @brief Returns volumetric clouds compute pipeline.
	 */
	ID<GraphicsPipeline> getComputePipeline();
	/**
	 * @brief Returns volumetric clouds view blend graphics pipeline.
	 */
	ID<GraphicsPipeline> getViewBlendPipeline();
};

} // namespace garden
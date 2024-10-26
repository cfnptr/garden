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

/***********************************************************************************************************************
 * @file
 * @brief Deferred rendering functions.
 * 
 * @details
 * 
 * G-Buffer structure:
 * 0. SrgbR8G8B8A8     (Base Color, Metallic)
 * 1. UnormA2B10G10R10 (Encoded Normal, Reflectance)
 * 2. UnormR8G8B8A8    (Emissive, Roughness)
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Deferred rendering system.
 * 
 * @details
 * Deferred rendering is a technique used in rendering engines to efficiently manage the rendering of complex 
 * scenes with many lights and materials. Unlike forward rendering, where each object in the scene is processed 
 * and shaded for every light in the scene, deferred rendering splits the rendering process into two major stages: 
 * geometry pass and lighting pass. This approach allows more flexibility in handling multiple lights without the 
 * significant performance hit typical in forward rendering.
 * 
 * Registers events: PreDeferredRender, DeferredRender, PreHdrRender, HdrRender,
 *   PreLdrRender, LdrRender, PreSwapchainRender, GBufferRecreate.
 */
class DeferredRenderSystem final : public System, public Singleton<DeferredRenderSystem>
{
public:
	/**
	 * @brief Deferred rendering G-Buffer count.
	 */
	static constexpr uint8 gBufferCount = 3;
private:
	vector<ID<Image>> gBuffers;
	ID<Image> hdrBuffer = {};
	ID<Image> ldrBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Framebuffer> gFramebuffer = {};
	ID<Framebuffer> hdrFramebuffer = {};
	ID<Framebuffer> ldrFramebuffer = {};
	ID<Framebuffer> toneMappingFramebuffer = {};
	bool asyncRecording = false;

	/**
	 * @brief Creates a new deferred rendering system instance.
	 * 
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param setSingleton set system singleton instance
	 */
	DeferredRenderSystem(bool useAsyncRecording = true, bool setSingleton = true);
	/**
	 * @brief Destroys deferred rendering system instance.
	 */
	~DeferredRenderSystem() final;

	void init();
	void deinit();
	void render();
	void swapchainRecreate();

	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Is deferred rendering enabled.
	 */
	bool isEnabled = true;
	/**
	 * @brief Run deferred rendering swapchain pass.
	 */
	bool runSwapchainPass = true;

	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }

	/**
	 * @brief Returns deferred G-Buffer array.
	 * @details It containts encoded deferred rendering data.
	 */
	const vector<ID<Image>>& getGBuffers();
	/**
	 * @brief Returns deferred HDR buffer. (High Dynamic Range)
	 */
	ID<Image> getHdrBuffer();
	/**
	 * @brief Returns deferred LDR buffer. (Low Dynamic Range)
	 */
	ID<Image> getLdrBuffer();
	/**
	 * @brief Returns deferred depth/stencil buffer.
	 */
	ID<Image> getDepthStencilBuffer();

	/**
	 * @brief Returns deferred G-Buffer framebuffer.
	 */
	ID<Framebuffer> getGFramebuffer();
	/**
	 * @brief Returns deferred HDR framebuffer. (High Dynamic Range)
	 */
	ID<Framebuffer> getHdrFramebuffer();
	/**
	 * @brief Returns deferred LDR framebuffer. (Low Dynamic Range)
	 */
	ID<Framebuffer> getLdrFramebuffer();
};

} // namespace garden
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
	ID<Image> gBuffers[gBufferCount] = {};
	ID<Image> hdrBuffer = {};
	ID<Image> ldrBuffer = {};
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
	bool isEnabled = true;
	bool runSwapchainPass = true;

	bool useAsyncRecording() const noexcept { return asyncRecording; }

	ID<Image>* getGBuffers();
	ID<Image> getHdrBuffer();
	ID<Image> getLdrBuffer();

	ID<Framebuffer> getGFramebuffer();
	ID<Framebuffer> getHdrFramebuffer();
	ID<Framebuffer> getLdrFramebuffer();
};

} // namespace garden
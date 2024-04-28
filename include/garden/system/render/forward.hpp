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
 * @brief Forward rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

using namespace garden::graphics;

/**
 * @brief Forward rendering system.
 * 
 * @details
 * 
 * Registers events: PreForwardRender, ForwardRender, PreSwapchainRender, ColorBufferRecreate.
 */
class ForwardRenderSystem final : public System
{
	ID<Image> colorBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Framebuffer> framebuffer = {};
	bool asyncRecording = false;
	bool hdrColorBuffer = false;

	/**
	 * @brief Creates a new forward rendering system instance.
	 * 
	 * @param[in,out] manager manager instance
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param useHdrColorBuffer create color buffer with extended color range
	 */
	ForwardRenderSystem(Manager* manager, bool useAsyncRecording = true, bool useHdrColorBuffer = false);
	/**
	 * @brief Destroys forward rendering system instance.
	 */
	~ForwardRenderSystem();

	void init();
	void deinit();
	void render();
	void swapchainRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true;
	bool runSwapchainPass = true;

	bool useAsyncRecording() const noexcept { return asyncRecording; }
	bool useHdrColorBuffer() const noexcept { return hdrColorBuffer; }

	ID<Image> getColorBuffer();
	ID<Image> getDepthStencilBuffer();
	ID<Framebuffer> getFramebuffer();
};

} // namespace garden
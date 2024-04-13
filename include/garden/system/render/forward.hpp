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

using namespace garden;
using namespace garden::graphics;

/**
 * @brief Forward rendering system.
 */
class ForwardRenderSystem final : public System
{
	ID<Image> colorBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Framebuffer> framebuffer = {};
	int2 framebufferSize = int2(0);
	float renderScale = 1.0f;
	bool asyncRecording = false;
	bool hdrColorBuffer = false;
	bool stencilBuffer = false;

	/**
	 * @brief Creates a new forward rendering system instance.
	 * 
	 * @param[in,out] manager manager instance
	 * @param asyncRecording use multithreaded render commands recording
	 * @param useHdrColorBuffer create color buffer with extended color range
	 * @param useStencilBuffer create stencil buffer along with depth buffer
	 */
	ForwardRenderSystem(Manager* manager, bool asyncRecording = true,
		bool useHdrColorBuffer = false, bool useStencilBuffer = false);
	/**
	 * @brief Destroys forward rendering system instance.
	 */
	~ForwardRenderSystem();

	void preInit();
	void postDeinit();
	void render();
	void swapchainRecreate();

	friend class ecsm::Manager;
	friend class DeferredEditorSystem;
public:
	bool isEnabled = true;
	bool runSwapchainPass = true;

	int2 getFramebufferSize() const noexcept { return framebufferSize; }
	bool useAsyncRecording() const noexcept { return asyncRecording; }
	bool useHdrColorBuffer() const noexcept { return stencilBuffer; }
	bool useStencilBuffer() const noexcept { return stencilBuffer; }
	float getRenderScale() const noexcept { return renderScale; }
	void setRenderScale(float renderScale);

	ID<Image> getColorBuffer();
	ID<Image> getDepthStencilBuffer();
	ID<Framebuffer> getFramebuffer();
};

} // namespace garden
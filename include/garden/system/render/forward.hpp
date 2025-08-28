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
 * @brief Forward rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Forward rendering system.
 * 
 * @details
 * Forward rendering is a traditional technique in graphics engines for rendering scenes, where the 
 * lighting and shading calculations are done for each object as it is drawn. Unlike deferred rendering, 
 * which separates geometry and lighting passes, forward rendering performs all the work 
 * (geometry, lighting, shading) in a single pass for each object.
 * 
 * Registers events:
 *   PreForwardRender, ForwardRender, 
 *   PreDepthForwardRender, DepthForwardRender, 
 *   PreUiRender, UiRender, 
 *   ColorBufferRecreate.
 */
class ForwardRenderSystem final : public System, public Singleton<ForwardRenderSystem>
{
public:
	static constexpr Image::Format colorBufferFormat = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format hdrBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format uiBufferFormat = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format depthStencilFormat = Image::Format::UnormD16;

	static constexpr Framebuffer::OutputAttachment::Flags colorBufferFlags = { false, false, true };
	static constexpr Framebuffer::OutputAttachment::Flags hdrBufferFlags = { false, false, true };
	static constexpr Framebuffer::OutputAttachment::Flags uiBufferFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags depthBufferFlags = { true, false, true };
private:
	ID<Image> colorBuffer = {};
	ID<Image> uiBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Framebuffer> colorFramebuffer = {};
	ID<Framebuffer> fullFramebuffer = {};
	ID<Framebuffer> uiFramebuffer = {};
	bool asyncRecording = false;
	bool hdrColorBuffer = false;

	/**
	 * @brief Creates a new forward rendering system instance.
	 * 
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param useHdrColorBuffer create color buffer with extended color range
	 * @param setSingleton set system singleton instance
	 */
	ForwardRenderSystem(bool useAsyncRecording = true, bool useHdrColorBuffer = false, bool setSingleton = true);
	/**
	 * @brief Destroys forward rendering system instance.
	 */
	~ForwardRenderSystem() final;

	void init();
	void deinit();
	void render();
	void swapchainRecreate();

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is forward rendering enabled. */

	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }
	/**
	 * @brief Use HDR color buffer for forward rendering. (High Dynamic Range)
	 */
	bool useHdrColorBuffer() const noexcept { return hdrColorBuffer; }

	/**
	 * @brief Returns forward color buffer.
	 */
	ID<Image> getColorBuffer();
	/**
	 * @brief Returns forward color buffer.
	 */
	ID<Image> getUiBuffer();
	/**
	 * @brief Returns forward depth/stencil buffer.
	 */
	ID<Image> getDepthStencilBuffer();

	/**
	 * @brief Returns color only forward framebuffer.
	 */
	ID<Framebuffer> getColorFramebuffer();
	/**
	 * @brief Returns color and depth forward framebuffer.
	 */
	ID<Framebuffer> getFullFramebuffer();
	/**
	 * @brief Returns UI forward framebuffer. (User Interface)
	 */
	ID<Framebuffer> getUiFramebuffer();
};

} // namespace garden
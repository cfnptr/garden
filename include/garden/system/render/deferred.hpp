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
 * @brief Deferred rendering functions.
 * 
 * @details
 * 
 * G-Buffer structure:
 *   0. SrgbB8G8R8A8     (Base Color, unused)
 *   1. UnormB8G8R8A8    (Metallic, Roughness, Ambient Occlusion, Reflectance)
 *   2. UnormB8G8R8A8    (Clear Coat, Clear Coat Roughness, unused, unused)
 *   3. UnormA2B10G10R10 (Encoded Normal, Shadow)
 *   4. SrgbB8G8R8A8     (Emissive Color and Factor) [optional]
 *   5. SrgbB8G8R8A8     (Subsurface Color, Thickness) [optional]
 */

// TODO: clear coat and sheen rendering. Make emissive, subsurface buffer creation optional.

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
 * Registers events:
 *   PreDeferredRender, DeferredRender, 
 *   PreHdrRender, HdrRender, 
 *   PreMetaHdrRender, MetaHdrRender, 
 *   PreOitRender, OitRender, 
 *   PreLdrRender, LdrRender, 
 *   PreMetaLdrRender, MetaLdrRender, 
 *   PreUiRender, UiRender, 
 *   PreSwapchainRender, GBufferRecreate.
 */
class DeferredRenderSystem final : public System, public Singleton<DeferredRenderSystem>
{
public:
	/**
	 * @brief Deferred rendering G-Buffer count.
	 * @details See the deferred.hpp header.
	 */
	static constexpr uint8 gBufferCount = 6;

	static constexpr uint8 baseColorGBuffer = 0;   /**< Index of the G-Buffer with encoded base color. */
	static constexpr uint8 opacityGBuffer = 0;     /**< Index of the G-Buffer with encoded opacity or transmission. */
	static constexpr uint8 metallicGBuffer = 1;    /**< Index of the G-Buffer with encoded metallic. */
	static constexpr uint8 roughnessGBuffer = 1;   /**< Index of the G-Buffer with encoded roughness. */
	static constexpr uint8 materialAoGBuffer = 1;  /**< Index of the G-Buffer with encoded material ambient occlusion. */
	static constexpr uint8 reflectanceGBuffer = 1; /**< Index of the G-Buffer with encoded reflectance. */
	static constexpr uint8 clearCoatGBuffer = 2;   /**< Index of the G-Buffer with encoded clear coat. */
	static constexpr uint8 ccRoughnessGBuffer = 2; /**< Index of the G-Buffer with encoded clear coat roughness. */
	static constexpr uint8 normalsGBuffer = 3;     /**< Index of the G-Buffer with encoded normals. */
	static constexpr uint8 shadowGBuffer = 3;      /**< Index of the G-Buffer with encoded shadow. */
	static constexpr uint8 emColorGBuffer = 4;     /**< Index of the G-Buffer with encoded emissive color. */
	static constexpr uint8 emFactorGBuffer = 4;    /**< Index of the G-Buffer with encoded emissive factor. */
	static constexpr uint8 subsurfaceGBuffer = 5;  /**< Index of the G-Buffer with encoded subsurface color. */
	static constexpr uint8 thicknessGBuffer = 5;   /**< Index of the G-Buffer with encoded thickness. */

	static constexpr Image::Format gBufferFormat0 = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format gBufferFormat1 = Image::Format::UnormB8G8R8A8;
	static constexpr Image::Format gBufferFormat2 = Image::Format::UnormB8G8R8A8;
	static constexpr Image::Format gBufferFormat3 = Image::Format::UnormA2B10G10R10;
	static constexpr Image::Format gBufferFormat4 = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format gBufferFormat5 = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format depthStencilFormat = Image::Format::SfloatD32;
	static constexpr Image::Format hdrBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format ldrBufferFormat = Image::Format::UnormB8G8R8A8;
	static constexpr Image::Format uiBufferFormat = Image::Format::UnormB8G8R8A8;
	static constexpr Image::Format oitAccumBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format oitRevealBufferFormat = Image::Format::UnormR8;
private:
	vector<ID<Image>> gBuffers;
	ID<Image> hdrBuffer = {};
	ID<Image> ldrBuffer = {};
	ID<Image> uiBuffer = {};
	ID<Image> oitAccumBuffer = {};
	ID<Image> oitRevealBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Framebuffer> gFramebuffer = {};
	ID<Framebuffer> hdrFramebuffer = {};
	ID<Framebuffer> metaHdrFramebuffer = {};
	ID<Framebuffer> ldrFramebuffer = {};
	ID<Framebuffer> metaLdrFramebuffer = {};
	ID<Framebuffer> uiFramebuffer = {};
	ID<Framebuffer> oitFramebuffer = {};
	bool asyncRecording = false;
	bool emissive = false;
	bool sss = false;

	/**
	 * @brief Creates a new deferred rendering system instance.
	 * 
	 * @param useEmissive use emissive buffer
	 * @param useSSS use sub surface scattering
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param setSingleton set system singleton instance
	 */
	DeferredRenderSystem(bool useEmissive = true, bool useSSS = true, 
		bool useAsyncRecording = true, bool setSingleton = true);
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
	bool isEnabled = true;        /**< Is deferred rendering enabled. */

	/**
	 * @brief Use multithreaded command buffer recording.
	 * @warning Be careful when writing asynchronous code!
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }

	/**
	 * @brief Use emissive buffer.
	 */
	bool useEmissive() const noexcept { return emissive; }
	/**
	 * @brief Use sub surface scattering.
	 */
	bool useSSS() const noexcept { return sss; }

	/**
	 * @brief Returns deferred G-Buffer array.
	 * @details It contains encoded deferred rendering data.
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
	 * @brief Returns deferred UI buffer. (User Interface)
	 */
	ID<Image> getUiBuffer();
	/**
	 * @brief Returns deferred OIT accumulation buffer. (Order Independent Transparency)
	 */
	ID<Image> getOitAccumBuffer();
	/**
	 * @brief Returns deferred OIT revealage buffer. (Order Independent Transparency)
	 */
	ID<Image> getOitRevealBuffer();
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
	 * @brief Returns deferred meta HDR framebuffer. (HDR + Depth)
	 */
	ID<Framebuffer> getMetaHdrFramebuffer();
	/**
	 * @brief Returns deferred LDR framebuffer. (Low Dynamic Range)
	 */
	ID<Framebuffer> getLdrFramebuffer();
	/**
	 * @brief Returns deferred meta LDR framebuffer. (LDR + Depth)
	 */
	ID<Framebuffer> getMetaLdrFramebuffer();
	/**
	 * @brief Returns deferred UI framebuffer. (User Interface)
	 */
	ID<Framebuffer> getUiFramebuffer();
	/**
	 * @brief Returns deferred OIT framebuffer. (Order Independent Transparency)
	 */
	ID<Framebuffer> getOitFramebuffer();
};

} // namespace garden
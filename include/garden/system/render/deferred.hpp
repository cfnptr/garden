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
 *   0. SrgbB8G8R8A8       (Base Color, Specular Factor)
 *   1. UnormB8G8R8A8      (Metallic, Roughness, Ambient Occlusion, Reflectance)
 *   2. UnormA2B10G10R10   (Encoded Normal, Shadow)
 *   3. UnormA2B10G10R10   (Clear Coat Normal and Roughness) [optional]
 *   4. SrgbB8G8R8A8       (Emissive Color and Factor) [optional]
 */

// TODO: Sheen rendering.

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
 *   PreDepthHdrRender, DepthHdrRender, 
 *   PreRefractedRender, RefractedRender, 
 *   PreTranslucentRender, TranslucentRender, 
 *   PreTransDepthRender, TransDepthRender, 
 *   PreOitRender, OitRender, 
 *   PreLdrRender, LdrRender, 
 *   PreDepthLdrRender, DepthLdrRender, 
 *   PreUiRender, UiRender, 
 *   GBufferRecreate.
 */
class DeferredRenderSystem final : public System, public Singleton<DeferredRenderSystem>
{
public:
	static constexpr uint8 gBufferBaseColor = 0;   /**< Index of the G-Buffer with encoded base color. */
	static constexpr uint8 gBufferSpecFactor = 0;  /**< Index of the G-Buffer with encoded specular factor. */
	static constexpr uint8 gBufferMetallic = 1;    /**< Index of the G-Buffer with encoded metallic. */
	static constexpr uint8 gBufferRoughness = 1;   /**< Index of the G-Buffer with encoded roughness. */
	static constexpr uint8 gBufferMaterialAO = 1;  /**< Index of the G-Buffer with encoded material ambient occlusion. */
	static constexpr uint8 gBufferReflectance = 1; /**< Index of the G-Buffer with encoded reflectance. */
	static constexpr uint8 gBufferNormals = 2;     /**< Index of the G-Buffer with encoded normals. */
	static constexpr uint8 gBufferShadow = 2;      /**< Index of the G-Buffer with encoded shadow. */
	static constexpr uint8 gBufferCcNormals = 3;   /**< Index of the G-Buffer with encoded clear coat normals. */
	static constexpr uint8 gBufferCcRoughness = 3; /**< Index of the G-Buffer with encoded clear coat roughness. */
	static constexpr uint8 gBufferEmColor = 4;     /**< Index of the G-Buffer with encoded emissive color. */
	static constexpr uint8 gBufferEmFactor = 4;    /**< Index of the G-Buffer with encoded emissive factor. */
	static constexpr uint8 gBufferCount = 5;       /**< Deferred rendering G-Buffer count. */

	static constexpr Image::Format gBufferFormat0 = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format gBufferFormat1 = Image::Format::UnormB8G8R8A8;
	static constexpr Image::Format gBufferFormat2 = Image::Format::UnormA2B10G10R10;
	static constexpr Image::Format gBufferFormat3 = Image::Format::UnormA2B10G10R10;
	static constexpr Image::Format gBufferFormat4 = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format depthStencilFormat = Image::Format::SfloatD32UintS8;
	static constexpr Image::Format depthFormat = Image::Format::SfloatD32;
	static constexpr Image::Format stencilFormat = Image::Format::UintS8;
	static constexpr Image::Format hdrBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format ldrBufferFormat = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format uiBufferFormat = Image::Format::SrgbB8G8R8A8;
	static constexpr Image::Format oitAccumBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format oitRevealBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format transBufferFormat = Image::Format::UnormR8;

	static constexpr Framebuffer::OutputAttachment::Flags gBufferFlags = { false, false, true};
	static constexpr Framebuffer::OutputAttachment::Flags gBufferDepthFlags = { true, false, true};
	static constexpr Framebuffer::OutputAttachment::Flags hdrBufferFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags hdrBufferDepthFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags ldrBufferFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags ldrBufferDepthFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags uiBufferFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags oitBufferFlags = { true, false, true };
	static constexpr Framebuffer::OutputAttachment::Flags oitBufferDepthFlags = { false, true, false };
	static constexpr Framebuffer::OutputAttachment::Flags normalsBufferFlags = { false, true, true };
	static constexpr Framebuffer::OutputAttachment::Flags transBufferFlags = { true, false, true};
	static constexpr Framebuffer::OutputAttachment::Flags transBufferDepthFlags = { false, true, true };
private:
	vector<ID<Image>> gBuffers;
	ID<Image> hdrBuffer = {};
	ID<Image> hdrCopyBuffer = {};
	ID<Image> ldrBuffer = {};
	ID<Image> uiBuffer = {};
	ID<Image> oitAccumBuffer = {};
	ID<Image> oitRevealBuffer = {};
	ID<Image> depthStencilBuffer = {};
	ID<Image> depthCopyBuffer = {};
	ID<Image> transBuffer = {};
	ID<ImageView> depthStencilIV = {};
	ID<ImageView> depthCopyIV = {};
	ID<ImageView> depthImageView = {};
	ID<ImageView> stencilImageView = {};
	ID<Framebuffer> gFramebuffer = {};
	ID<Framebuffer> hdrFramebuffer = {};
	ID<Framebuffer> depthHdrFramebuffer = {};
	ID<Framebuffer> ldrFramebuffer = {};
	ID<Framebuffer> depthLdrFramebuffer = {};
	ID<Framebuffer> uiFramebuffer = {};
	ID<Framebuffer> refractedFramebuffer = {};
	ID<Framebuffer> oitFramebuffer = {};
	ID<Framebuffer> transDepthFramebuffer = {};
	bool asyncRecording = false;
	bool hasStencil = false;
	bool hasClearCoat = false;
	bool hasEmission = false;
	bool hasAnyRefr = false;
	bool hasAnyOit = false;
	bool hasAnyTD = false;

	/**
	 * @brief Creates a new deferred rendering system instance.
	 * 
	 * @param useStencil use stencil buffer
	 * @param useClearCoat use clear coat buffer
	 * @param useEmission use light emission buffer
	 * @param useAsyncRecording use multithreaded render commands recording
	 * @param setSingleton set system singleton instance
	 */
	DeferredRenderSystem(bool useStencil = false, bool useClearCoat = true, 
		bool useEmission = true, bool useAsyncRecording = true, bool setSingleton = true);
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
	 * @brief Use clear coat buffer.
	 */
	bool useClearCoat() const noexcept { return hasClearCoat; }
	/**
	 * @brief Use light emission buffer.
	 */
	bool useEmission() const noexcept { return hasEmission; }

	/**
	 * @brief Marks that there is rendered refraction data on the current frame.
	 */
	void markAnyRefraction() noexcept { hasAnyRefr = true; }
	/**
	 * @brief Returns if there is rendered refraction data on the current frame.
	 */
	bool hasAnyRefraction() const noexcept { return hasAnyRefr; }
	/**
	 * @brief Marks that there is rendered OIT data on the current frame.
	 */
	void markAnyOIT() noexcept { hasAnyOit = true; }
	/**
	 * @brief Returns if there is rendered OIT data on the current frame.
	 */
	bool hasAnyOIT() const noexcept { return hasAnyOit; }
	/**
	 * @brief Marks that there is rendered translucent depth data on the current frame.
	 */
	void markAnyTransDepth() noexcept { hasAnyTD = true; }
	/**
	 * @brief Returns if there is rendered translucent depth data on the current frame.
	 */
	bool hasAnyTransDepth() const noexcept { return hasAnyTD; }

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
	 * @brief Returns deferred HDR copy buffer. (High Dynamic Range)
	 */
	ID<Image> getHdrCopyBuffer();
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
	 * @brief Returns deferred depth/stencil copy buffer.
	 */
	ID<Image> getDepthCopyBuffer();
	/**
	 * @brief Returns deferred transparent buffer.
	 */
	ID<Image> getTransBuffer();

	/**
	 * @brief Returns deferred depth/stencil buffer image view.
	 */
	ID<ImageView> getDepthStencilIV();
	/**
	 * @brief Returns deferred depth/stencil copy buffer image view.
	 */
	ID<ImageView> getDepthCopyIV();
	/**
	 * @brief Returns deferred depth buffer image view.
	 */
	ID<ImageView> getDepthImageView();
	/**
	 * @brief Returns deferred stencil buffer image view.
	 */
	ID<ImageView> getStencilImageView();

	/**
	 * @brief Returns deferred G-Buffer framebuffer.
	 */
	ID<Framebuffer> getGFramebuffer();
	/**
	 * @brief Returns deferred HDR framebuffer. (High Dynamic Range)
	 */
	ID<Framebuffer> getHdrFramebuffer();
	/**
	 * @brief Returns deferred depth HDR framebuffer. (HDR + Depth)
	 */
	ID<Framebuffer> getDepthHdrFramebuffer();
	/**
	 * @brief Returns deferred LDR framebuffer. (Low Dynamic Range)
	 */
	ID<Framebuffer> getLdrFramebuffer();
	/**
	 * @brief Returns deferred depth LDR framebuffer. (LDR + Depth)
	 */
	ID<Framebuffer> getDepthLdrFramebuffer();
	/**
	 * @brief Returns deferred UI framebuffer. (User Interface)
	 */
	ID<Framebuffer> getUiFramebuffer();
	/**
	 * @brief Returns deferred refracted framebuffer. (HDR + Depth + Normals)
	 */
	ID<Framebuffer> getRefractedFramebuffer();
	/**
	 * @brief Returns deferred OIT framebuffer. (Order Independent Transparency)
	 */
	ID<Framebuffer> getOitFramebuffer();
	/**
	 * @brief Returns deferred transparent depth framebuffer.
	 */
	ID<Framebuffer> getTransDepthFramebuffer();
};

} // namespace garden
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
 * @brief Deferred rendering functions.
 * 
 * @details
 * 
 * G-Buffer structure:
 *   0. SrgbR8G8B8A8       (Base/Emissive Color, Material ID)
 *   1. UnormR8G8B8A8      (Metallic, Roughness, AO/CC/Emissive, Shadow)
 *   2. UnormA2B10G10R10   (Encoded Normal, Reflectance/Specular)
 *   3. SfloatR16G16       (Velocity) [optional]
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
 * Registers events:
 *   PreDeferredRender, DeferredRender, 
 *   PreHdrRender, HdrRender, 
 *   PreDepthHdrRender, DepthHdrRender, 
 *   PreRefrRender, RefractedRender, 
 *   PreTransRender, TranslucentRender, 
 *   PreTransDepthRender, TransDepthRender, 
 *   PreOitRender, OitRender, 
 *   PreLdrRender, LdrRender, 
 *   PreDepthLdrRender, DepthLdrRender, 
 *   PostLdrToUI, PreUiRender, UiRender, 
 *   GBufferRecreate.
 */
class DeferredRenderSystem final : public System, public Singleton<DeferredRenderSystem>
{
public:
	/**
	 * @brief Deferred rendering system initialization options.
	 */
	struct Options final
	{
		bool useStencil = true;        /**< Create and use stencil buffer for rendering. */
		bool useVelocity = true;       /**< Create and use reflection buffer for rendering. */
		bool useDisoccl = true;        /**< Create and use disocclusion map for rendering. */
		bool useAsyncRecording = true; /**< Use multithreaded render commands recording. */
		Options() noexcept { }
	};

	struct DisocclPC final
	{
		float nearPlane;
		float threshold;
		float velFactor;
	};

	static constexpr Image::Format gBufferFormat0 = Image::Format::SrgbR8G8B8A8;
	static constexpr Image::Format gBufferFormat1 = Image::Format::UnormR8G8B8A8;
	static constexpr Image::Format gBufferFormat2 = Image::Format::UnormA2B10G10R10;
	static constexpr Image::Format gBufferFormat3 = Image::Format::SfloatR16G16;
	static constexpr Image::Format depthStencilFormat = Image::Format::SfloatD32UintS8;
	static constexpr Image::Format depthFormat = Image::Format::SfloatD32;
	static constexpr Image::Format stencilFormat = Image::Format::UintS8;
	static constexpr Image::Format hdrBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format ldrBufferFormat = Image::Format::SrgbR8G8B8A8;
	static constexpr Image::Format uiBufferFormat = Image::Format::SrgbR8G8B8A8;
	static constexpr Image::Format oitAccumBufferFormat = Image::Format::SfloatR16G16B16A16;
	static constexpr Image::Format oitRevealBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format transBufferFormat = Image::Format::UnormR8;
	static constexpr Image::Format disocclMapFormat = Image::Format::UnormR8;

	static constexpr Framebuffer::OutputAttachment::Flags gBufferFlags = { false, false, true };
	static constexpr Framebuffer::OutputAttachment::Flags gBufferDepthFlags = { true, false, true };
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
	static constexpr Framebuffer::OutputAttachment::Flags disocclMapFlags = { false, false, true};
private:
	vector<ID<Image>> gBuffers;
	ID<Image> hdrBuffer = {}, hdrCopyBuffer = {};
	ID<Image> ldrBuffer = {}, uiBuffer = {}, disocclMap = {};
	ID<Image> oitAccumBuffer = {}, oitRevealBuffer = {};
	ID<Image> depthStencilBuffer = {}, depthCopyBuffer = {};
	ID<Image> transBuffer = {}, upscaleHdrBuffer = {};
	ID<ImageView> depthStencilIV = {};
	ID<ImageView> depthCopyIV = {};
	ID<ImageView> depthImageView = {};
	ID<ImageView> stencilImageView = {};
	ID<ImageView> hdrCopyIV = {};
	ID<Framebuffer> gFramebuffer = {};
	ID<Framebuffer> hdrFramebuffer = {};
	ID<Framebuffer> depthHdrFB = {};
	ID<Framebuffer> ldrFramebuffer = {};
	ID<Framebuffer> depthLdrFB = {};
	ID<Framebuffer> uiFramebuffer = {};
	ID<Framebuffer> oitFramebuffer = {};
	ID<Framebuffer> transDepthFB = {};
	ID<Framebuffer> upscaleHdrFB = {};
	ID<Framebuffer> disocclusionFB = {};
	ID<GraphicsPipeline> velocityPipeline = {};
	ID<GraphicsPipeline> disocclPipeline = {};
	ID<GraphicsPipeline> hdrCopyBlurPipeline = {};
	ID<DescriptorSet> velocityDS = {}, disocclDS = {};
	vector<ID<Framebuffer>> hdrCopyBlurFBs;
	vector<ID<DescriptorSet>> hdrCopyBlurDSes;
	Options options = {};
	bool hasAnyRefr = false;
	bool hasAnyOit = false;
	bool hasAnyTD = false;

	/**
	 * @brief Creates a new deferred rendering system instance.
	 * 
	 * @param options target system initialization options
	 * @param setSingleton set system singleton instance
	 */
	DeferredRenderSystem(Options options = {}, bool setSingleton = true);
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
	bool isEnabled = true;         /**< Is deferred rendering enabled. */
	float disocclThreshold = 0.1f; /**< Disocclusion detection threshold. */
	float disocclVelFactor = 2.0f; /**< Disocclusion velocity multiplier. */

	/*******************************************************************************************************************
	 * @brief Returns deferred rendering system options.
	 */
	Options getOptions() const noexcept { return options; }
	/**
	 * @brief Enables or disables use of the specific system rendering options.
	 * @details It destroys existing buffers on use set to false.
	 * @param options target rendering system options
	 */
	void setOptions(Options options);

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
	 * @brief Returns deferred camera velocity graphics pipeline.
	 */
	ID<GraphicsPipeline> getVelocityPipeline();
	/**
	 * @brief Returns deferred disocclusion graphics pipeline.
	 */
	ID<GraphicsPipeline> getDisocclPipeline();

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
	 * @brief Returns deferred upscale HDR buffer.
	 */
	ID<Image> getUpscaleHdrBuffer();
	/**
	 * @brief Returns deferred disocclusion map.
	 */
	ID<Image> getDisocclMap();

	/**
	 * @brief Returns deferred HDR buffer image view. (High Dynamic Range)
	 */
	ID<ImageView> getHdrImageView();
	/**
	 * @brief Returns deferred HDR copy buffer image view. (High Dynamic Range)
	 */
	ID<ImageView> getHdrCopyIV();
	/**
	 * @brief Returns deferred LDR buffer image view. (Low Dynamic Range)
	 */
	ID<ImageView> getLdrImageView();
	/**
	 * @brief Returns deferred UI buffer image view. (User Interface)
	 */
	ID<ImageView> getUiImageView();
	/**
	 * @brief Returns deferred OIT accumulation buffer image view. (Order Independent Transparency)
	 */
	ID<ImageView> getOitAccumIV();
	/**
	 * @brief Returns deferred OIT revealage buffer image view. (Order Independent Transparency)
	 */
	ID<ImageView> getOitRevealIV();
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
	 * @brief Returns deferred transparent buffer image view.
	 */
	ID<ImageView> getTransImageView();
	/**
	 * @brief Returns deferred upscale HDR buffer image view.
	 */
	ID<ImageView> getUpscaleHdrIV();
	/**
	 * @brief Returns deferred disocclusion map image view.
	 * @param mip target image view mipmap level
	 */
	ID<ImageView> getDisocclView(uint8 mip);

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
	ID<Framebuffer> getDepthHdrFB();
	/**
	 * @brief Returns deferred LDR framebuffer. (Low Dynamic Range)
	 */
	ID<Framebuffer> getLdrFramebuffer();
	/**
	 * @brief Returns deferred depth LDR framebuffer. (LDR + Depth)
	 */
	ID<Framebuffer> getDepthLdrFB();
	/**
	 * @brief Returns deferred UI framebuffer. (User Interface)
	 */
	ID<Framebuffer> getUiFramebuffer();
	/**
	 * @brief Returns deferred OIT framebuffer. (Order Independent Transparency)
	 */
	ID<Framebuffer> getOitFramebuffer();
	/**
	 * @brief Returns deferred transparent depth framebuffer.
	 */
	ID<Framebuffer> getTransDepthFB();
	/**
	 * @brief Returns deferred upscale HDR framebuffer.
	 */
	ID<Framebuffer> getUpscaleHdrFB();
	/**
	 * @brief Returns deferred disocclusion framebuffer.
	 */
	ID<Framebuffer> getDisocclusionFB();
	/**
	 * @brief Returns deferred HDR copy blur framebuffers.
	 */
	const vector<ID<Framebuffer>>& getHdrCopyBlurFBs();
};

} // namespace garden
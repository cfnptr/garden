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
 * @brief Common graphics framebuffer functions.
 */

#pragma once
#include "garden/graphics/image.hpp"

namespace garden::graphics
{

using namespace ecsm;

class Pipeline;
class FramebufferExt;

/***********************************************************************************************************************
 * @brief Rendering destinations container.
 * 
 * @details
 * Framebuffer is a rendering destination that encapsulates a collection of image views representing 
 * the attachments to which rendering will happen. These attachments typically include color, 
 * depth and stencil buffers. The framebuffer object itself does not contain the image data, 
 * instead, it references the image views that are the actual storage for these buffers.
 */
class Framebuffer final : public Resource
{
public:
	/**
	 * @brief Framebuffer input attachment description.
	 * @details See the @ref Framebuffer::getSubpasses().
	 * @warning shaderStages variable affects memory syncronization!
	 */
	struct InputAttachment final
	{
		ID<ImageView> imageView = {};  /**< Input attachment image view. */
		ShaderStage shaderStages = {}; /**< Shader stages where attachment is used. [affects synchronization!] */

		/**
		 * @brief Creates a new framebuffer input attachment.
		 * 
		 * @param imageView target input image view
		 * @param shaderStages shader stages where attachment is used [affects synchronization!]
		 */
		InputAttachment(ID<ImageView> imageView, ShaderStage shaderStages) noexcept
		{ this->imageView = imageView; this->shaderStages = shaderStages; }
		/**
		 * @brief Creates a new empty framebuffer input attachment.
		 * @note It can not be used to create a framebuffer.
		 */
		InputAttachment() { }
	};
	/**
	 * @brief Framebuffer output attachment description.
	 * @details See the @ref Framebuffer::getColorAttachments().
	 * @warning clear, load and store variables are per attachment, not subpass!
	 */
	struct OutputAttachment final
	{
		ID<ImageView> imageView = {}; /**< Output attachment image view. */
		bool clear = false;           /**< Clear output attachment content before rendering. */
		bool load = false;            /**< Load output attachment content before rendering. */
		bool store = false;           /**< Store output attachment content after rendering. */

		/**
		 * @brief Creates a new framebuffer input attachment.
		 * 
		 * @param imageView target output image view
		 * @param clear clear output attachment content before rendering
		 * @param load load output attachment content before rendering
		 * @param store store output attachment content after rendering
		 */
		OutputAttachment(ID<ImageView> imageView, bool clear, bool load, bool store) noexcept
		{
			this->imageView = imageView;
			this->clear = clear;
			this->load = load;
			this->store = store;
		}
		/**
		 * @brief Creates a new empty framebuffer output attachment.
		 * @note It can not be used to create a framebuffer.
		 */
		OutputAttachment() { }
	};

	/*******************************************************************************************************************
	 * @brief Framebuffer subpass description.
	 * 
	 * @details
	 * Subpass represents a phase of rendering that produces specific outputs or performs certain operations using 
	 * shared resources. Each subpass can read from and write to attachments (like color buffers, depth buffers, etc.) 
	 * that were set up in the framebuffer when the render pass was defined. Improves performance on tiled GPUs.
	 * 
	 * See the @ref Framebuffer::getColorAttachments().
	 */
	struct Subpass final
	{
		vector<InputAttachment> inputAttachments;   /**< Subpass input attachment array. */
		vector<OutputAttachment> outputAttachments; /**< Subpass output attachment array. */
		PipelineType pipelineType = {};             /**< Rendering pipeline type to use. */

		/**
		 * @brief Creates a new framebuffer subpass.
		 * 
		 * @param pipelineType rendering pipeline type to use
		 * @param[in] inputAttachments subpass input attachment array
		 * @param[in] outputAttachments subpass output attachment array
		 */
		Subpass(PipelineType pipelineType, const vector<InputAttachment>& inputAttachments,
			const vector<OutputAttachment>& outputAttachments) noexcept
		{
			this->inputAttachments = inputAttachments;
			this->outputAttachments = outputAttachments;
			this->pipelineType = pipelineType;
		}
		/**
		 * @brief Creates a new empty framebuffer subpass.
		 * @note It can not be used to create a framebuffer.
		 */
		Subpass() { }
	};
	/**
	 * @brief Framebuffer subpass attachment container.
	 * @details See the @ref Framebuffer::recreate().
	 * @note Attachment array sizes should be the same as in the created framebuffer.
	 */
	struct SubpassImages final
	{
		vector<ID<ImageView>> inputAttachments;  /**< A new subpass input attachment array. */
		vector<ID<ImageView>> outputAttachments; /**< A new subpass output attachment array. */

		/**
		 * @brief Creates a new framebuffer subpass images container.
		 * 
		 * @param[in] inputAttachments a new subpass input attachment array
		 * @param[in] outputAttachments a new subpass output attachment array
		 */
		SubpassImages(const vector<ID<ImageView>>& inputAttachments,
			const vector<ID<ImageView>>& outputAttachments) noexcept
		{
			this->inputAttachments = inputAttachments;
			this->outputAttachments = outputAttachments;
		}
		/**
		 * @brief Creates a new empty framebuffer subpass images container.
		 * @note It can not be used to recreate a subpass.
		 */
		SubpassImages() { }
	};

	/*******************************************************************************************************************
	 * @brief Depth/stencil color container.
	 */
	struct DepthStencilValue final
	{
		float depth = 0.0f;    /**< Depth buffer value. */
		uint32 stencil = 0x00; /**< Stencil buffer value. */
	};
	/**
	 * @brief Attachment clear color container.
	 */
	union ClearColor final
	{
		float4 floatValue = float4(0.0f);   /**< Floating point clear color. */
		int4 intValue;                      /**< Signed integer clear color. */
		DepthStencilValue deptStencilValue; /**< Depth/stencil clear value. */
	};
	/**
	 * @brief Clear attachment description.
	 * @details See the @ref Framebuffer::clearAttachments().
	 */
	struct ClearAttachment final
	{
		uint32 index = 0;      /**< Framebuffer attachment index. */
		ClearColor color = {}; /**< Attachment clear color. (infill) */
	};
	/**
	 * @brief Attachment clear region description.
	 * @details See the @ref Framebuffer::clearAttachments().
	 */
	struct ClearRegion final
	{
		int2 offset = int2(0); /**< Region offset in texels. */
		int2 extent = int2(0); /**< Region extent in texels. */
		uint32 baseLayer = 0;  /**< Image base array layer. */
		uint32 layerCount = 0; /**< Image array layer count. */
	};

private:
	//******************************************************************************************************************
	void* renderPass = nullptr;
	vector<Subpass> subpasses;
	vector<OutputAttachment> colorAttachments;
	int2 size = int2(0);
	OutputAttachment depthStencilAttachment = {};
	bool isSwapchain = false;

	// Note: Use GraphicsSystem to create, destroy and access framebuffers.

	Framebuffer() = default;
	Framebuffer(int2 size, vector<Subpass>&& subpasses);
	Framebuffer(int2 size, vector<OutputAttachment>&& colorAttachments,
		OutputAttachment depthStencilAttachment);
	Framebuffer(int2 size, ID<ImageView> swapchainImage)
	{
		this->instance = (void*)1;
		this->colorAttachments.push_back(
			OutputAttachment(swapchainImage, false, true, true));
		this->size = size;
		this->isSwapchain = true;
	}
	bool destroy() final;

	static ID<Framebuffer> currentFramebuffer;
	static uint32 currentSubpassIndex;
	static vector<ID<Pipeline>> currentPipelines;
	static vector<PipelineType> currentPipelineTypes;
	static vector<ID<Buffer>> currentVertexBuffers;
	static vector<ID<Buffer>> currentIndexBuffers;

	friend class Pipeline;
	friend class CommandBuffer;
	friend class FramebufferExt;
	friend class GraphicsPipeline;
	friend class LinearPool<Framebuffer>;
public:
	/*******************************************************************************************************************
	 * @brief Returns framebuffer size in texels.
	 * @details All attachments should have this size.
	 */
	int2 getSize() const noexcept { return size; }
	/**
	 * @brief Returns framebuffer color attachments.
	 * @details Images within a framebuffer where the output color data from rendering operations is stored.
	 */
	const vector<OutputAttachment>& getColorAttachments() const noexcept { return colorAttachments; }
	/**
	 * @brief Returns framebuffer depth/stencil attachment.
	 * @details Image within a framebuffer where the output depth/stencil data from rendering operations is stored.
	 */
	const OutputAttachment& getDepthStencilAttachment() const noexcept { return depthStencilAttachment; }
	/**
	 * @brief Returns framebuffer subpasses.
	 * 
	 * @details
	 * Advanced feature designed to optimize rendering by organizing the rendering process into multiple, 
	 * sequential steps that share the same framebuffer. These subpasses are part of a larger structure 
	 * known as a "render pass", which defines how the graphics pipeline will handle contents of the 
	 * framebuffer throughout the various stages of rendering. Improves performance on tiled GPUs.
	 */
	const vector<Subpass>& getSubpasses() const noexcept { return subpasses; }
	/**
	 * @brief Is framebuffer part og the swapchain.
	 * @details Swapchain framebuffers are provided by the graphics API.
	 */
	bool isSwapchainFramebuffer() const noexcept { return isSwapchain; }

	/*******************************************************************************************************************
	 * @brief Updates framebufer attachments.
	 * @note This operation is fast when dynamic rendering is supported.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorAttachments color attachment array
	 * @param colorAttachmentCount color attachment array size
	 * @param depthStencilAttachment depth stencil attachment or empty
	 */
	void update(int2 size, const OutputAttachment* colorAttachments,
		uint32 colorAttachmentCount, OutputAttachment depthStencilAttachment = {});
	/**
	 * @brief Updates framebufer attachments.
	 * @note This operation is fast when dynamic rendering is supported.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorAttachments color attachment array
	 * @param depthStencilAttachment depth stencil attachment or empty
	 */
	void update(int2 size, vector<OutputAttachment>&& colorAttachments,
		OutputAttachment depthStencilAttachment = {});
	
	/**
	 * @brief Recreates framebuffer subpasses.
	 * @warning Use only when required, this operation impacts performance!
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] subpasses target subpass array
	 */
	void recreate(int2 size, const vector<SubpassImages>& subpasses);

	/**
	 * @brief Returns current render pass framebuffer.
	 * @details Set by the @ref Framebuffer::beginRenderPass().
	 */
	static ID<Framebuffer> getCurrent() noexcept { return currentFramebuffer; }
	/**
	 * @brief Returns current render subpass index.
	 * @details Changes by the @ref Framebuffer::nextSubpass().
	 */
	static uint8 getCurrentSubpassIndex() noexcept { return currentSubpassIndex; }
	/**
	 * @brief Is rendering with multithreaded commands recording.
	 * @details Changes by the @ref Framebuffer::nextSubpass().
	 */
	static bool isCurrentRenderPassAsync() noexcept;

	#if GARDEN_DEBUG
	/**
	 * @brief Sets framebuffer debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Begins framebuffer rendering pass.
	 * 
	 * @details
	 * This command initiates a block of operations where rendering takes place within a defined set of framebuffer 
	 * attachments. It sets up the necessary state and resources to perform rendering and defines how the framebuffer 
	 * attachments (like color, depth and stencil buffers) are to be handled during the rendering pass.
	 * 
	 * @note Clearing at the beginning the render pass is faster than clearing attachments or images.
	 * 
	 * @param[in] clearColors attachment clear color array or null
	 * @param clearColorCount clear color array size or 0
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param[in] region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const float4* clearColors = nullptr, uint8 clearColorCount = 0, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, const int4& region = int4(0), bool asyncRecording = false);
	
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 * 
	 * @tparam N clear color array size
	 * @param[in] clearColors attachment clear color array
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param[in] region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	template<psize N>
	void beginRenderPass(const array<float4, N>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, const int4& region = int4(0), bool asyncRecording = false)
	{ beginRenderPass(clearColors.data(), (uint8)N, clearDepth, clearStencil, region, asyncRecording); }
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 * 
	 * @param[in] clearColors attachment clear color vector
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param[in] region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const vector<float4>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, const int4& region = int4(0), bool asyncRecording = false)
	{
		beginRenderPass(clearColors.data(), (uint8)clearColors.size(),
			clearDepth, clearStencil, region, asyncRecording);
	}
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 * 
	 * @param[in] clearColor attachment clear color
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param[in] region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const float4& clearColor, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, const int4& region = int4(0), bool asyncRecording = false)
	{ beginRenderPass(&clearColor, 1, clearDepth, clearStencil, region, asyncRecording); }
	
	/*******************************************************************************************************************
	 * @brief Proceeds to the next framebuffer subpass.
	 * @param asyncRecording render with multithreaded commands recording
	 * 
	 * @details
	 * Subpasses are a feature that allows multiple rendering operations to be 
	 * efficiently batched together into a single render pass with multiple steps.
	 */
	void nextSubpass(bool asyncRecording = false);

	/**
	 * @brief Ends framebuffer render pass.
	 * 
	 * @details
	 * Concludes a render pass that was initiated with a @ref Framebuffer::beginRenderPass() command. This command 
	 * is essential for properly finalizing the sequence of operations within a render pass, ensuring that all rendering 
	 * outputs are correctly handled and that the GPU is ready to proceed with other tasks or another render pass.
	 */
	void endRenderPass();

	/*******************************************************************************************************************
	 * @brief Clears framebuffer attachments content.
	 * 
	 * @details
	 * Clears the contents of specific attachments within a framebuffer during a render pass. 
	 * This command is particularly useful when you need to reset the contents of a color, depth, 
	 * or stencil attachment to a known value at specific points in a render pass, without 
	 * having to clear the entire framebuffer or end and begin a new render pass.
	 * 
	 * @param[in] attachments framebuffer attachment array
	 * @param attachmentCount attachment array size
	 * @param[in] regions image clear region array
	 * @param regionCount region array size
	 */
	void clearAttachments(const ClearAttachment* attachments, uint8 attachmentCount,
		const ClearRegion* regions, uint32 regionCount);

	/**
	 * @brief Clears framebuffer attachments content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * 
	 * @tparam A attachment array size
	 * @tparam R region array size
	 * @param[in] attachments framebuffer attachment array
	 * @param[in] regions image clear region array
	 */
	template<psize A, psize R>
	void clearAttachments(const array<ClearAttachment, A>& attachments, const array<ClearRegion, R>& regions)
	{ clearAttachments(attachments.data(), (uint8)A, regions.data(), (uint32)R); }
	/**
	 * @brief Clears framebuffer attachments content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * 
	 * @param[in] attachments framebuffer attachment vector
	 * @param[in] regions image clear region vector
	 */
	void clearAttachments(const vector<ClearAttachment>& attachments, const vector<ClearRegion>& regions)
	{ clearAttachments(attachments.data(), (uint8)attachments.size(), regions.data(), (uint32)regions.size()); }

	/**
	 * @brief Clears framebuffer attachment content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * 
	 * @param attachments framebuffer attachment
	 * @param regions image clear region
	 */
	void clearAttachment(ClearAttachment attachment, const ClearRegion& region)
	{ clearAttachments(&attachment, 1, &region, 1); }
	/**
	 * @brief Clears framebuffer attachment content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * @param attachments framebuffer attachment
	 */
	void clearAttachment(ClearAttachment attachment)
	{ ClearRegion region; clearAttachments(&attachment, 1, &region, 1); }
};

/***********************************************************************************************************************
 * @brief Graphics framebuffer resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class FramebufferExt final
{
public:
	/**
	 * @brief Returns framebuffer render pass instance.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static void*& getRenderPass(Framebuffer& framebuffer) noexcept { return framebuffer.renderPass; }
	/**
	 * @brief Returns framebuffer subpasses.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static vector<Framebuffer::Subpass>& getSubpasses(Framebuffer& framebuffer)
		noexcept { return framebuffer.subpasses; }
	/**
	 * @brief Returns framebuffer color attachments.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static vector<Framebuffer::OutputAttachment>& getColorAttachments(Framebuffer& framebuffer)
		noexcept { return framebuffer.colorAttachments; }
	/**
	 * @brief Returns framebuffer size in texels.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static int2& getSize(Framebuffer& framebuffer) noexcept { return framebuffer.size; }
	/**
	 * @brief Returns framebuffer depth/stencil attachment.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static Framebuffer::OutputAttachment& getDepthStencilAttachment(Framebuffer& framebuffer)
		noexcept { return framebuffer.depthStencilAttachment; }
	/**
	 * @brief Is framebuffer part of the swapchain.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static bool& isSwapchain(Framebuffer& framebuffer) noexcept { return framebuffer.isSwapchain; }
};

} // namespace garden::graphics
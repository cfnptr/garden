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
 * @brief Common graphics framebuffer functions.
 */

 // TODO: implement VK_KHR_dynamic_rendering_local_read.

#pragma once
#include "garden/graphics/image.hpp"

namespace garden::graphics
{

class FramebufferExt;

/**
 * @brief Rendering destinations container.
 * 
 * @details
 * Framebuffer is a rendering destination that encapsulates a collection of image views representing 
 * the attachments to which rendering will happen. These attachments typically include color, 
 * depth and stencil buffers. The framebuffer object itself does not contain the image data, 
 * instead, it references the image views that are the actual storage for these buffers.
 */
class Framebuffer final
{
public:
	/**
	 * @brief Framebuffer attachment content load operations.
	 * @details Specifies how contents of an attachment are treated at the beginning of the rendering.
	 */
	enum class LoadOp : uint8
	{
		Load,     /**< Load existing content of an attachment image. */
		Clear,    /**< Clear render pass content with specified value. */
		DontCare, /**< Existing attachment image content may be undefined. */
		None,     /**< Undefined existing attachment image content. (Read only) */
		Count,    /**< Framebuffer attachment content load operation count. */
	};
	/**
	 * @brief Framebuffer attachment content store operations.
	 * @details Specifies how contents of an attachment are treated at the end of the rendering.
	 */
	enum class StoreOp : uint8
	{
		Store,    /**< Write render pass generated content to the memory. */
		DontCare, /**< Render pass generated content may be discarded. */
		None,     /**< Render pass does not generate any content. (Read only) */
		Count,    /**< Framebuffer attachment content store operation count. */
	};

	/**
	 * @brief Framebuffer attachment properties.
	 * @details See the @ref Framebuffer::getColorAttachments().
	 */
	struct Attachment final
	{
		ID<ImageView> imageView = {}; /**< Framebuffer attachment image view instance. */
		LoadOp loadOperation = {};    /**< Existing attachment content load operation. */
		StoreOp storeOperation = {};  /**< Generated attachment content store operation. */
		uint16 _alignment = 0;

		/**
		 * @brief Creates a new framebuffer attachment.
		 * 
		 * @param imageView target framebuffer attachment image view
		 * @param loadOperation attachment content load operation
		 * @param storeOperation attachment content store operation
		 */
		constexpr Attachment(ID<ImageView> imageView = {}, 
			LoadOp loadOperation = LoadOp::Load, StoreOp storeOperation = StoreOp::Store) noexcept :
			imageView(imageView), loadOperation(loadOperation), storeOperation(storeOperation) { }
	};

	/*******************************************************************************************************************
	 * @brief Framebuffer depth/stencil color container.
	 */
	struct DepthStencilValue final
	{
		float depth = 0.0f;    /**< Depth buffer value. */
		uint32 stencil = 0x00; /**< Stencil buffer value. */
	};
	/**
	 * @brief Framebuffer attachment clear color container.
	 */
	union ClearColor final
	{
		float4 floatValue = float4::zero;     /**< Floating point clear color. */
		int4 intValue;                     /**< Signed integer clear color. */
		uint4 uintValue;                    /**< Unsigned integer clear color. */
		DepthStencilValue deptStencilValue; /**< Depth/stencil clear value. */
	};
	/**
	 * @brief Framebuffer clear attachment properties.
	 * @details See the @ref Framebuffer::clearAttachments().
	 */
	struct ClearAttachment final
	{
		uint32 index = 0;           /**< Framebuffer attachment index. */
		ClearColor clearColor = {}; /**< Attachment clear color. (infill) */
	};
	/**
	 * @brief Framebuffer attachment clear region properties.
	 * @details See the @ref Framebuffer::clearAttachments().
	 */
	struct ClearRegion final
	{
		uint2 offset = uint2::zero; /**< Region offset in texels. */
		uint2 extent = uint2::zero; /**< Region extent in texels. */
		uint32 baseLayer = 0;       /**< Image base array layer. */
		uint32 layerCount = 0;      /**< Image array layer count. */
	};
private:
	vector<Attachment> colorAttachments;
	Attachment depthStencilAttachment = {};
	uint2 size = uint2::zero;
	uint32 depthStencilLayout = 0;
	bool isSwapchain = false;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	string debugName = UNNAMED_RESOURCE;
	#endif

	Framebuffer(uint2 size, vector<Attachment>&& colorAttachments, Attachment depthStencilAttachment);
	Framebuffer(uint2 size, ID<ImageView> swapchainImage)
	{
		this->colorAttachments = { Attachment(swapchainImage, LoadOp::Load, StoreOp::Store) };
		this->size = size;
		this->isSwapchain = true;
	}
	bool destroy() { return true; }

	friend class FramebufferExt;
	friend class LinearPool<Framebuffer>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty framebuffer data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access framebuffers.
	 */
	Framebuffer() noexcept = default;

	/**
	 * @brief Returns framebuffer size in texels.
	 * @details All framebuffer attachments should have this size.
	 */
	uint2 getSize() const noexcept { return size; }
	/**
	 * @brief Returns framebuffer color attachments.
	 * @details Images within a framebuffer where the color data from rendering operations are stored.
	 */
	const vector<Attachment>& getColorAttachments() const noexcept { return colorAttachments; }
	/**
	 * @brief Returns framebuffer depth/stencil attachment.
	 * @details Image within a framebuffer where the depth/stencil data from rendering operations are stored.
	 */
	const Attachment& getDepthStencilAttachment() const noexcept { return depthStencilAttachment; }

	/**
	 * @brief Returns true if framebuffer contains color or depth stencil attachments.
	 */
	bool isValid() const noexcept { return !colorAttachments.empty() || depthStencilAttachment.imageView; }
	/**
	 * @brief Is this framebuffer part of the swapchain.
	 * @details Swapchain framebuffers are provided by the graphics API.
	 */
	bool isSwapchainFramebuffer() const noexcept { return isSwapchain; }

	/**
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorAttachments color attachment array or null
	 * @param colorAttachmentCount color attachment array size
	 * @param depthStencilAttachment depth stencil attachment or empty
	 */
	void update(uint2 size, const Attachment* colorAttachments,
		uint32 colorAttachmentCount, Attachment depthStencilAttachment = {});
	/**
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorAttachments color attachment array or empty
	 * @param depthStencilAttachment depth stencil attachment or empty
	 */
	void update(uint2 size, vector<Attachment>&& colorAttachments, Attachment depthStencilAttachment = {});
	/**
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param colorAttachment color attachment or empty
	 * @param depthStencilAttachment depth stencil attachment or empty
	 */
	void update(uint2 size, Attachment colorAttachment, Attachment depthStencilAttachment = {})
	{
		update(size, &colorAttachment, 1, depthStencilAttachment);
	}

	/*******************************************************************************************************************
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorImageViews color image view array or null
	 * @param colorImageViewCount color image view array size
	 * @param depthStencilIV depth stencil image view or empty
	 */
	void update(uint2 size, const ID<ImageView>* colorImageViews,
		uint32 colorImageViewCount, ID<ImageView> depthStencilIV = {});
	/**
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param[in] colorImageViews a new color image view array or empty
	 * @param depthStencilIV a new depth stencil image view or empty
	 */
	void update(uint2 size, const vector<ID<ImageView>>& colorImageViews, ID<ImageView> depthStencilIV= {})
	{
		update(size, colorImageViews.data(), colorImageViews.size(), depthStencilIV);
	}
	/**
	 * @brief Updates framebuffer attachments.
	 * 
	 * @param size a new framebuffer size in texels
	 * @param colorImageView a new color image view or empty
	 * @param depthStencilIV a new depth stencil image view or empty
	 */
	void update(uint2 size, ID<ImageView> colorImageView, ID<ImageView> depthStencilIV = {})
	{
		update(size, &colorImageView, 1, depthStencilIV);
	}

	/**
	 * @brief Updates framebuffer color attachment at specified index.
	 *
	 * @param index target framebuffer color attachment index
	 * @param[in] colorAttachment a new color attachment data
	 */
	void updateColor(uint32 index, const Attachment& colorAttachment);
	/**
	 * @brief Updates framebuffer color attachment at specified index.
	 *
	 * @param index target framebuffer color attachment index
	 * @param colorImageView a new color image view or empty
	 */
	void updateColor(uint32 index, ID<ImageView> colorImageView);
	/**
	 * @brief Updates framebuffer color attachment at specified index.
	 *
	 * @param index target framebuffer color attachment index
	 * @param loadOperation a new attachment content load operation
	 * @param storeOperation a new attachment content store operation
	 */
	void updateColor(uint32 index, LoadOp loadOperation, StoreOp storeOperation);

	/**
	 * @brief Updates framebuffer depth stencil attachment.
	 * @param[in] depthStencilAttachment a new depth stencil attachment data
	 */
	void updateDepthStencil(const Attachment& depthStencilAttachment);
	/**
	 * @brief Updates framebuffer depth stencil attachment.
	 * @param depthStencilIV a new depth stencil image view or empty
	 */
	void updateDepthStencil(ID<ImageView> depthStencilIV);
	/**
	 * @brief Updates framebuffer depth stencil attachment.
	 *
	 * @param loadOperation a new attachment content load operation
	 * @param storeOperation a new attachment content store operation
	 */
	void updateDepthStencil(LoadOp loadOperation, StoreOp storeOperation);

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
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const float4* clearColors = nullptr, uint8 clearColorCount = 0, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false);
	
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 * 
	 * @tparam N clear color array size
	 * @param[in] clearColors attachment clear color array
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	template<psize N>
	void beginRenderPass(const array<float4, N>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false)
	{ beginRenderPass(clearColors.data(), (uint8)N, clearDepth, clearStencil, region, asyncRecording); }
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 * 
	 * @param[in] clearColors attachment clear color vector
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const vector<float4>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false)
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
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	void beginRenderPass(const float4& clearColor, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false)
	{ beginRenderPass(&clearColor, 1, clearDepth, clearStencil, region, asyncRecording); }

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
	 * @param attachment framebuffer attachment
	 * @param region image clear region
	 */
	void clearAttachment(ClearAttachment attachment, const ClearRegion& region)
	{ clearAttachments(&attachment, 1, &region, 1); }
	/**
	 * @brief Clears framebuffer attachment content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * @param attachment target framebuffer attachment
	 */
	void clearAttachment(ClearAttachment attachment)
	{ ClearRegion region; clearAttachments(&attachment, 1, &region, 1); }
	/**
	 * @brief Clears first framebuffer attachment content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 */
	void clearAttachment()
	{ ClearAttachment attachment; ClearRegion region; clearAttachments(&attachment, 1, &region, 1); }

	/**
	 * @brief Clears framebuffer depth/stencil attachment content.
	 * @details See the @ref Framebuffer::clearAttachments().
	 * 
	 * @param depth attachment depth clear value
	 * @param stencil attachment stencil clear value
	 */
	void clearDepthStencilAttachment(float depth = 0.0f, uint32 stencil = 0x00)
	{
		ClearAttachment attachment;
		attachment.index = (uint32)colorAttachments.size();
		attachment.clearColor.deptStencilValue.depth = depth;
		attachment.clearColor.deptStencilValue.stencil = stencil;
		clearAttachment(attachment);
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Returns framebuffer debug name. (Debug Only)
	 */
	const string& getDebugName() const noexcept { return debugName; }
	/**
	 * @brief Sets framebuffer debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name)
	{
		GARDEN_ASSERT(!name.empty());
		debugName = name;
	}
	#endif
};

/***********************************************************************************************************************
 * @brief Framebuffer attachment content load operation name strings
 */
constexpr const char* framebufferLoadOpNames[(psize)Framebuffer::LoadOp::Count] =
{
	"Load", "Clear", "DontCare", "None"
};
/**
 * @brief Framebuffer attachment content load operation name strings
 */
constexpr const char* framebufferStoreOpNames[(psize)Framebuffer::StoreOp::Count] =
{
	"Store", "DontCare", "None"
};

/**
 * @brief Returns framebuffer attachment content load operation name string.
 * @param loadOperation target framebuffer load operation
 */
static string_view toString(Framebuffer::LoadOp loadOperation) noexcept
{
	GARDEN_ASSERT(loadOperation < Framebuffer::LoadOp::Count);
	return framebufferLoadOpNames[(psize)loadOperation];
}
/**
 * @brief Returns framebuffer attachment content store operation name string.
 * @param storeOperation target framebuffer store operation
 */
static string_view toString(Framebuffer::StoreOp storeOperation) noexcept
{
	GARDEN_ASSERT(storeOperation < Framebuffer::StoreOp::Count);
	return framebufferStoreOpNames[(psize)storeOperation];
}

/***********************************************************************************************************************
 * @brief Framebuffer render pass abstraction class.
 */
class RenderPass final
{
	ID<Framebuffer> framebuffer = {};
public:
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
	 * @param framebuffer target framebuffer instance
	 * @param[in] clearColors attachment clear color array or null
	 * @param clearColorCount clear color array size or 0
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	RenderPass(ID<Framebuffer> framebuffer, const float4* clearColors = nullptr, uint8 clearColorCount = 0,
		float clearDepth = 0.0f, uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false);
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 *
	 * @tparam N clear color array size
	 * @param framebuffer target framebuffer instance
	 * @param[in] clearColors attachment clear color array
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	template<psize N>
	RenderPass(ID<Framebuffer> framebuffer, const array<float4, N>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false) :
		RenderPass(framebuffer, clearColors.data(), (uint8)N, clearDepth, clearStencil, region, asyncRecording) { }
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 *
	 * @param framebuffer target framebuffer instance
	 * @param[in] clearColors attachment clear color vector
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	RenderPass(ID<Framebuffer> framebuffer, const vector<float4>& clearColors, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false) :
		RenderPass(framebuffer, clearColors.data(), (uint8)clearColors.size(), 
			clearDepth, clearStencil, region, asyncRecording) { }
	/**
	 * @brief Begins framebuffer rendering pass.
	 * @details See the @ref Framebuffer::beginRenderPass().
	 *
	 * @param framebuffer target framebuffer instance
	 * @param[in] clearColor attachment clear color
	 * @param clearDepth clear depth value or 0
	 * @param clearStencil clear stencil value or 0
	 * @param region rendering region (0 = full size)
	 * @param asyncRecording render with multithreaded commands recording
	 */
	RenderPass(ID<Framebuffer> framebuffer, const float4& clearColor, float clearDepth = 0.0f,
		uint32 clearStencil = 0x00, int4 region = int4::zero, bool asyncRecording = false) : 
		RenderPass(framebuffer, &clearColor, 1, clearDepth, clearStencil, region, asyncRecording) { }
	/**
	 * @brief Ends framebuffer render pass.
	 *
	 * @details
	 * Concludes a render pass that was initiated with a @ref Framebuffer::beginRenderPass() command. This command
	 * is essential for properly finalizing the sequence of operations within a render pass, ensuring that all rendering
	 * outputs are correctly handled and that the GPU is ready to proceed with other tasks or another render pass.
	 */
	~RenderPass();
};

/***********************************************************************************************************************
 * @brief Graphics framebuffer resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class FramebufferExt final
{
public:
	/**
	 * @brief Returns framebuffer color attachments.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static vector<Framebuffer::Attachment>& getColorAttachments(Framebuffer& framebuffer)
		noexcept { return framebuffer.colorAttachments; }
	/**
	 * @brief Returns framebuffer size in texels.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static uint2& getSize(Framebuffer& framebuffer) noexcept { return framebuffer.size; }
	/**
	 * @brief Returns framebuffer depth/stencil attachment.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static Framebuffer::Attachment& getDepthStencilAttachment(Framebuffer& framebuffer)
		noexcept { return framebuffer.depthStencilAttachment; }
	/**
	 * @brief Returns framebuffer depth stencil attachment layout
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static uint32& getDepthStencilLayout(Framebuffer& framebuffer) noexcept { return framebuffer.depthStencilLayout; }
	/**
	 * @brief Is this framebuffer part of the swapchain.
	 * @warning In most cases you should use @ref Framebuffer functions.
	 * @param[in] framebuffer target framebuffer instance
	 */
	static bool& isSwapchain(Framebuffer& framebuffer) noexcept { return framebuffer.isSwapchain; }
};

} // namespace garden::graphics
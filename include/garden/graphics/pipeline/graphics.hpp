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
 * @brief Graphics pipeline functions.
 */

#pragma once
#include "garden/graphics/pipeline.hpp"
#include "garden/graphics/framebuffer.hpp"

namespace garden::graphics
{

class GraphicsPipelineExt;

/**
 * @brief Graphics rendering stages container.
 * 
 * @details
 * Graphics pipeline is used for rendering operations. It is a highly configurable series of stages that process 
 * vertex data into pixel data to be output to a framebuffer. The stages of a typical graphics pipeline include:
 * 
 * Input Assembler: Collects raw vertex data from buffers and may also use an index buffer to reuse vertex data.
 * Vertex Shader: Processes each vertex, performing operations such as transformations and lighting calculations.
 * Rasterization: Converts the geometry into fragments (potential pixels) for further processing.
 * Fragment Shader: Processes each fragment to determine its final color and other attributes.
 * Depth and Stencil Testing: Performs depth comparisons and stencil operations to determine if a fragment should be discarded.
 * Color Blending: Combines the fragment's color with the color in the framebuffer based on various blending operations.
 * Output Merger: Finalizes the processed fragments and writes them to the framebuffer attachments.
 */
class GraphicsPipeline final : public Pipeline
{
public:
	/**
	 * @brief Primitive topologies.
	 * 
	 * @details
	 * The way in which vertices (the basic units of geometry in 3D graphics) are organized and 
	 * interpreted to form shapes or primitives in computer graphics. This concept is fundamental in 
	 * the rendering process, as it determines how a sequence of vertices is assembled into 
	 * geometric shapes that can ultimately be rasterized (converted into pixels on the screen).
	 */
	enum class Topology : uint8
	{
		TriangleList,  /**< Series of separate triangle primitives. */
		TriangleStrip, /**< Series of connected triangle primitives with consecutive triangles sharing an edge. */
		LineList,      /**< Series of separate line primitives. */
		LineStrip,     /**< Series of connected line primitives with consecutive lines sharing a vertex. */
		PointList,     /**< Series of separate point primitives. */
		Count          /**< Primitive topology type count. */
	};
	/**
	 * @brief Polygon rasterization mode.
	 * 
	 * @details
	 * Polygon rasterization mode refers to how the interior of polygons (typically triangles in 
	 * modern graphics) is filled during the rasterization process in computer graphics.
	 */
	enum class Polygon : uint8
	{
		Fill,  /**< Polygons are rendered using the polygon rasterization rules. */
		Line,  /**< Polygon edges are drawn as line segments. */
		Point, /**< Polygon vertices are drawn as points. */
		Count  /**< Polygon rasterization mode count. */
	};

	/*******************************************************************************************************************
	 * @brief Triangle culling mode.
	 * 
	 * @details
	 * Cull mode determines which faces of polygons (typically triangles) are not rendered based on 
	 * their orientation relative to the viewer. This state is a crucial part of the graphics pipeline, 
	 * as it helps to optimize rendering performance by eliminating the need to draw polygons that 
	 * are not visible to the camera, such as the back faces of objects in a 3D scene.
	 */
	enum class CullFace : uint8
	{
		Front,        /**< Front-facing triangles are discarded. */
		Back,         /**< Back-facing triangles are discarded. */
		FrontAndBack, /**< All triangles are discarded. */
		Count         /**< Triangle culling mode count. */
	};
	/**
	 * @brief Polygon front-facing orientation.
	 * 
	 * @details
	 * Side of the polygon that is considered to be facing towards the camera or the viewer. 
	 * This determination is essential for back-face culling, which is the process of excluding 
	 * the polygons facing away from the camera from the rendering process to improve performance.
	 */
	enum class FrontFace : uint8
	{
		Clockwise,        /**< Triangle with positive area is considered front-facing. */
		CounterClockwise, /**< Triangle with negative area is considered front-facing. */
		Count             /**< Polygon front-facing orientation count. */
	};

	/*******************************************************************************************************************
	 * @brief Framebuffer blending factors.
	 * 
	 * @details
	 * Crucial component of the blending process, which is used to control how the colors of a source pixel 
	 * (the pixel being rendered) and a destination pixel (the pixel already in the framebuffer) are combined. 
	 * Blending is often used to achieve various visual effects, such as transparency, translucency and 
	 * anti-aliasing, by mixing colors based on different proportions or factors.
	 * 
	 * finalColor = (sourceColor * sourceFactor) + (destinationColor * destinationFactor)
	 */
	enum class BlendFactor : uint8
	{
		Zero,               /**< (0, 0, 0, 0) */
		One,                /**< (1, 1, 1, 1) */
		SrcColor,           /**< (Rs, Gs, Bs, As) */
		OneMinusSrcColor,   /**< (1 - Rs, 1 - Gs, 1 - Bs, 1 - Ad) */
		DstColor,           /**< (Rd, Gd, Bd, Ad) */
		OneMinusDstColor,   /**< (1 - Rd, 1 - Gd, 1 - Bd, 1 - Ad) */
		SrcAlpha,           /**< (As, As, As, As) */
		OneMinusSrcAlpha,   /**< (1 - As, 1 - As, 1 - As, 1 - As) */
		DstAlpha,           /**< (Ad, Ad, Ad, Ad) */
		OneMinusDstAlpha,   /**< (1 - Ad, 1 - Ad, 1 - Ad, 1 - Ad) */
		ConstColor,         /**< (Rc, Gc, Bc, Ac) */
		OneMinusConstColor, /**< (1 - Rc, 1 - Gc, 1 - Bc, 1 - Ac) */
		ConstAlpha,         /**< (Ac, Ac, Ac, Ac) */
		OneMinusConstAlpha, /**< (1 - As, 1 - As, 1 - As, 1 - As) */
		Src1Color,          /**< (Rs1, Gs1, Bs1, As1) */
		OneMinusSrc1Color,  /**< (1 - Rs1, 1 - Gs1, 1 - Bs1, 1 - As1) */
		Src1Alpha,          /**< (As1, As1, As1, As1) */
		OneMinusSrc1Alpha,  /**< (1 - As1, 1 - As1, 1 - As1, 1 - As1) */
		SrcAlphaSaturate,   /**< (f, f, f, 1); f = min(As, 1 - Ad) */
		Count               /**< Framebuffer blending factor count. */
	};
	/**
	 * @brief Framebuffer blending operations.
	 * 
	 * @details
	 * Mathematical operation that combines the color of a source pixel (the pixel being drawn) with the color of 
	 * a destination pixel (the pixel already in the framebuffer) during the blending stage. This operation is 
	 * crucial for achieving various visual effects such as transparency, translucency, soft edges and more.
	 */
	enum class BlendOperation : uint8
	{
		Add,             /**< finalColor = (srcColor * srcBlendFactor) + (dstColor * dstBlendFactor) */
		Subtract,        /**< finalColor = (srcColor * srcBlendFactor) - (dstColor * dstBlendFactor) */
		ReverseSubtract, /**< finalColor = (dstColor * dstBlendFactor) - (srcColor * srcBlendFactor) */
		Minimum,         /**< finalColor = min(srcColor, dstColor) */
		Maximum,         /**< finalColor = max(srcColor, dstColor) */
		Count            /**< Framebuffer blending operation count. */
	};
	/**
	 * @brief Bitmask controlling which components are written to the framebuffer.
	 * 
	 * @details
	 * Allows to selectively enable or disable writing of individual color components (red, green, blue and alpha) 
	 * to the framebuffer during rendering operations. This mechanism provides fine-grained control over how 
	 * pixels are updated in the render target, enabling a range of graphical effects and optimizations by 
	 * manipulating which parts of a pixel's color can be altered.
	 * 
	 * @note The color write mask operation is applied regardless of whether blending is enabled.
	 */
	enum class ColorComponent : uint8
	{
		None = 0x00, /**< All color components in memory are unmodified. */
		R    = 0x01, /**< R value is written to the color attachment. Otherwise, the value in memory is unmodified. */
		G    = 0x02, /**< G value is written to the color attachment. Otherwise, the value in memory is unmodified. */
		B    = 0x04, /**< B value is written to the color attachment. Otherwise, the value in memory is unmodified. */
		A    = 0x08, /**< A value is written to the color attachment. Otherwise, the value in memory is unmodified. */
		All  = 0x0F, /**<  All components are written to the color attachment. */
	};

	static constexpr uint8 colorComponentCount = 4; /**< Color component count. */

	/*******************************************************************************************************************
	 * @brief Blending operations state for a framebuffer attachment.
	 * 
	 * @details
	 * Collection of settings that determine how blending is performed for a particular framebuffer attachment. 
	 * Blending is the process of combining the color of a source pixel (generated by a fragment shader, for example) 
	 * with the color of a destination pixel (already present in the framebuffer) based on various factors and 
	 * operations. The blend state controls how these two colors are mathematically combined, enabling a wide range of 
	 * visual effects such as transparency, translucency, additive lighting effects and more.
	 */
	struct BlendState final
	{
		bool blending = false;                                      /**< Is blending enabled for this attachment. */
		BlendFactor srcColorFactor = BlendFactor::SrcAlpha;         /**< Source color blending factor. */
		BlendFactor dstColorFactor = BlendFactor::OneMinusSrcAlpha; /**< Destination color blending factor. */
		BlendOperation colorOperation = BlendOperation::Add;        /**< Color components (R, G, B) blending operation. */
		BlendFactor srcAlphaFactor = BlendFactor::One;              /**< Source alpha blending factor. */
		BlendFactor dstAlphaFactor = BlendFactor::Zero;             /**< Destination alpha blending factor. */
		BlendOperation alphaOperation = BlendOperation::Add;        /**< Alpha component (A) blending operation. */
		ColorComponent colorMask = ColorComponent::All;             /**< Bitmask of the color components to write. */
	};
	/**
	 * @brief Graphics pipeline state.
	 * 
	 * @details
	 * Collection of configurations that dictate how the graphics pipeline processes and renders graphics 
	 * primitives (such as vertices and pixels) to produce the final image on the screen. This state encompasses 
	 * various settings and stages that control everything from how vertices are processed to how colors are 
	 * blended in the final framebuffer. The concept of a pipeline state is fundamental in modern 
	 * graphics APIs like Vulkan, DirectX 12 and Metal.
	 */
	struct State final
	{
		uint8 depthTesting : 1;                                        /**< Is depth value testing enabled. */
		uint8 depthWriting : 1;                                        /**< Is depth value writing enabled. */
		uint8 depthClamping : 1;                                       /**< Is depth value clamping enabled. */
		uint8 depthBiasing : 1;                                        /**< Is depth value biasing enabled. */
		uint8 stencilTesting : 1;                                      /**< Is stencil value testing enabled. */
		uint8 faceCulling : 1;                                         /**< Is face culling enabled. */
		uint8 discarding : 1;                                          /**< Is fragment discarding enabled. */
		uint8 _unused : 1;                                             /**< [reserved for future use] */
		Topology topology = Topology::TriangleList;                    /**< Primitive topology type. */
		Polygon polygon = Polygon::Fill;                               /**< Polygon rasterization mode. */
		Sampler::CompareOp depthCompare = Sampler::CompareOp::Greater; /**< Depth compare operation. */
		float depthBiasConstant = 0.0f;                                /**< Depth bias constant value. */
		float depthBiasClamp = 0.0f;                                   /**< Depth bias clamp value. */
		float depthBiasSlope = 0.0f;                                   /**< Depth bias slope value. */
		float4 blendConstant = float4::zero;                           /**< Blending operations constant color. */
		CullFace cullFace = CullFace::Back;                            /**< Triangle culling mode. */
		FrontFace frontFace = FrontFace::CounterClockwise;             /**< Polygon front-facing orientation. */
		uint16 _alignment = 0;                                         /**< [structure alignment] */
		// Note: Should be aligned.

		State() : depthTesting(0), depthWriting(0), depthClamping(0), depthBiasing(0), 
			stencilTesting(0), faceCulling(1), discarding(0), _unused(0) { }
	};

	using PipelineStates = tsl::robin_map<uint8, State>;
	using BlendStates = tsl::robin_map<uint8, vector<BlendState>>;

	/*******************************************************************************************************************
	 * @brief Vertex input attribute description.
	 * 
	 * @details
	 * Data associated with each vertex in a mesh that defines certain characteristics of that vertex. 
	 * These attributes are essential for rendering as they provide the necessary information to 
	 * the graphics pipeline about how to process and display each vertex in 3D space.
	 */
	struct VertexAttribute final
	{
		GslDataType type = {};     /**< Vertex attribute data type. */
		GslDataFormat format = {}; /**< Vertex attribute data format. */
		uint16 offset = 0;         /**< Byte offset of this attribute relative to the start of an element. */
		// Note: Should be aligned.
	};

	/**
	 * @brief Graphics pipeline shader code overrides.
	 * @details It allows to override pipeline shader code.
	 */
	struct ShaderOverrides final
	{
		vector<uint8> headerData;
		vector<uint8> vertexCode;
		vector<uint8> fragmentCode;
	};
	/**
	 * @brief Graphics pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct GraphicsCreateData : public CreateData
	{
		uint8 subpassIndex = 0;
		Image::Format depthStencilFormat = {};
		uint8 _alignment = 0;
		vector<uint8> vertexCode;
		vector<uint8> fragmentCode;
		vector<VertexAttribute> vertexAttributes;
		vector<BlendState> blendStates;
		vector<Image::Format> colorFormats;
		PipelineStates pipelineStateOverrides;
		BlendStates blendStateOverrides;
		void* renderPass = nullptr;
		State pipelineState = {};
		uint16 vertexAttributesSize = 0;
	};
private:
	uint8 _alignment = 0;
	uint8 attachmentCount = 0;
	uint8 subpassIndex = 0;
	ID<Framebuffer> framebuffer = {};

	GraphicsPipeline(const fs::path& path, uint32 maxBindlessCount, bool useAsyncRecording,
		uint64 pipelineVersion, ID<Framebuffer> framebuffer, uint8 subpassIndex) :
		Pipeline(PipelineType::Graphics, path, maxBindlessCount, useAsyncRecording, pipelineVersion),
		subpassIndex(subpassIndex), framebuffer(framebuffer) { }
	GraphicsPipeline(GraphicsCreateData& createData, bool useAsyncRecording);

	void createVkInstance(GraphicsCreateData& createData);

	friend class GraphicsPipelineExt;
	friend class LinearPool<GraphicsPipeline>;
public:
	/**
	 * @brief Creates a new empty graphics pipeline data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access graphics pipelines.
	 */
	GraphicsPipeline() = default;

	/**
	 * @brief Returns graphics pipeline parent framebuffer.
	 * @note We can use graphics pipeline only inside this framebuffer.
	 */
	ID<Framebuffer> getFramebuffer() const noexcept { return framebuffer; }
	/**
	 * @brief Returns graphics pipeline framebuffer color attachment count.
	 * @note It should be the same as target rendering framebuffer.
	 */
	uint8 getAttachmentCount() const noexcept { return attachmentCount; }
	/**
	 * @brief Returns graphics pipeline subpass index inside framebuffer pass.
	 * @details Sub passes help graphics API to optimize resources sharing, especially on tile-based GPUs.
	 */
	uint8 getSubpassIndex() const noexcept { return subpassIndex; }

	/**
	 * @brief Updates graphics pipeline parent framebuffer.
	 * @param framebuffer a new parent framebuffer
	 */
	void updateFramebuffer(ID<Framebuffer> framebuffer);

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Specifies the region of the framebuffer where the rendering will occur.
	 * @param viewport target viewport (xy = position, zw = size)
	 */
	void setViewport(float4 viewport = float4::zero);
	/**
	 * @brief Specifies the region of the framebuffer where the rendering will occur. (MT-Safe)
	 * @details See the @ref GraphicsPipeline::setViewport()
	 * 
	 * @param viewport target viewport value (xy = position, zw = size)
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void setViewportAsync(float4 viewport = float4::zero, int32 threadIndex = -1);

	/**
	 * @brief Defines a scissor rectangle, where rendering is allowed to occur.
	 * @param scissor target scissor value (xy = offset, zw = extent)
	 * 
	 * @details
	 * Any drawing operation outside this scissor rectangle is clipped and will not appear in the final image.
	 * This command is used in conjunction with the viewport setting to further restrict rendering to a specific 
	 * region of the screen, enabling more precise control over where graphics are drawn.
	 */
	void setScissor(int4 scissor = int4::zero);
	/**
	 * @brief Defines a scissor rectangle, where rendering is allowed to occur. (MT-Safe)
	 * @details See the @ref GraphicsPipeline::setScissor()
	 * 
	 * @param scissor target scissor value (xy = offset, zw = extent)
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void setScissorAsync(int4 scissor = int4::zero, int32 threadIndex = -1);

	/**
	 * @brief Specifies a viewport and scissor rendering regions.
	 * @details See the @ref GraphicsPipeline::setViewport() and @ref GraphicsPipeline::setScissor()
	 * 
	 * @param viewportScissor target viewport and scissor value (xy = position, zw = size)
	 */
	void setViewportScissor(float4 viewportScissor = float4::zero);
	/**
	 * @brief Specifies a viewport and scissor rendering regions. (MT-Safe)
	 * @details See the @ref GraphicsPipeline::setViewport() and @ref GraphicsPipeline::setScissor()
	 * 
	 * @param viewportScissor target viewport and scissor value (xy = position, zw = size)
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void setViewportScissorAsync(float4 viewportScissor = float4::zero, int32 threadIndex = -1);

	/*******************************************************************************************************************
	 * @brief Renders primitives to the framebuffer.
	 * 
	 * @details 
	 * Fundamental operation that instructs the GPU to render primitives (basic shapes like points, lines and 
	 * triangles) based on the provided vertex data and the current graphics pipeline state.
	 * 
	 * @param vertexBuffer target vertex buffer or null
	 * @param vertexCount vertex count to draw
	 * @param instanceCount draw instance count
	 * @param vertexOffset vertex offset in the buffer or 0
	 * @param instanceOffset draw instance offset or 0
	 */
	void draw(ID<Buffer> vertexBuffer, uint32 vertexCount, uint32 instanceCount = 1,
		uint32 vertexOffset = 0, uint32 instanceOffset = 0);
	/**
	 * @brief Renders primitives to the framebuffer. (MT-Safe)
	 * @details See the @ref GraphicsPipeline::draw()
	 * 
	 * @param threadIndex thread index in the pool
	 * @param vertexBuffer target vertex buffer or null
	 * @param vertexCount vertex count to draw
	 * @param instanceCount draw instance count
	 * @param vertexOffset vertex offset in the buffer or 0
	 * @param instanceOffset draw instance offset or 0
	 */
	void drawAsync(int32 threadIndex, ID<Buffer> vertexBuffer, uint32 vertexCount,
		uint32 instanceCount = 1, uint32 vertexOffset = 0, uint32 instanceOffset = 0);

	/**
	 * @brief Renders primitives based on indices to the framebuffer.
	 * 
	 * @details 
	 * Tells the GPU to render primitives (such as triangles, lines or points) based on indices into a 
	 * set of vertices. This command is particularly efficient for rendering complex geometries where 
	 * vertices are shared among multiple primitives.
	 * 
	 * @param vertexBuffer target vertex buffer
	 * @param indexBuffer target index buffer
	 * @param indexType type of the index data
	 * @param indexCount index count to draw
	 * @param instanceCount draw instance count
	 * @param indexOffset index offset in the buffer or 0
	 * @param vertexOffset vertex offset in the buffer or 0
	 * @param instanceOffset draw instance offset or 0
	 */
	void drawIndexed(ID<Buffer> vertexBuffer, ID<Buffer> indexBuffer,
		IndexType indexType, uint32 indexCount, uint32 instanceCount = 1,
		uint32 indexOffset = 0, uint32 vertexOffset = 0, uint32 instanceOffset = 0);
	/**
	 * @brief Renders primitives based on indices to the framebuffer.
	 * @details See the @ref GraphicsPipeline::drawIndexed()
	 * 
	 * @param threadIndex thread index in the pool
	 * @param vertexBuffer target vertex buffer
	 * @param indexBuffer target index buffer
	 * @param indexType type of the index data
	 * @param indexCount index count to draw
	 * @param instanceCount draw instance count
	 * @param indexOffset index offset in the buffer or 0
	 * @param vertexOffset vertex offset in the buffer or 0
	 * @param instanceOffset draw instance offset or 0
	 */
	void drawIndexedAsync(int32 threadIndex, ID<Buffer> vertexBuffer,
		ID<Buffer> indexBuffer, IndexType indexType, uint32 indexCount,
		uint32 instanceCount = 1, uint32 indexOffset = 0,
		uint32 vertexOffset = 0, uint32 instanceOffset = 0);

	/**
	 * @brief Renders fullscreen triangle to the framebuffer.
	 * @details Useful for a full screen post processing effects.
	 */
	void drawFullscreen();
	/**
	 * @brief Renders fullscreen triangle to the framebuffer.
	 * @details See the @ref GraphicsPipeline::drawFullscreen()
	 * @param threadIndex thread index in the pool
	 */
	void drawFullscreenAsync(int32 threadIndex);

	/**
	 * @brief Set depth bias factors and clamp dynamically.
	 * @details Useful for shadow mapping.
	 * 
	 * @param constantFactor scalar factor controlling the constant depth value added to each fragment
	 * @param slopeFactor scalar factor applied to a fragment’s slope in depth bias calculations
	 * @param clamp maximum (or minimum) depth bias of a fragment
	 */
	static void setDepthBias(float constantFactor, float slopeFactor, float clamp = 0.0f);

	/**
	 * @brief Set depth bias factors and clamp dynamically.
	 * @details See the @ref GraphicsPipeline::setDepthBias()
	 * 
	 * @param constantFactor scalar factor controlling the constant depth value added to each fragment
	 * @param slopeFactor scalar factor applied to a fragment’s slope in depth bias calculations
	 * @param clamp maximum (or minimum) depth bias of a fragment
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	static void setDepthBiasAsync(float constantFactor, float slopeFactor, float clamp = 0.0f, int32 threadIndex = -1);

	// TODO: Also allow to override blendStates, vertexAttributes separately.
	// TODO: Add dynamic and dynamic/static states (viewport/scissor).
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(GraphicsPipeline::ColorComponent)

/**
 * @brief Returns primitive topology type.
 * @param topology target primitive topology name string (camelCase)
 * @throw GardenError on unknown primitive topology type.
 */
static GraphicsPipeline::Topology toTopology(string_view topology)
{
	if (topology == "triangleList") return GraphicsPipeline::Topology::TriangleList;
	if (topology == "triangleStrip") return GraphicsPipeline::Topology::TriangleStrip;
	if (topology == "lineList") return GraphicsPipeline::Topology::LineList;
	if (topology == "lineStrip") return GraphicsPipeline::Topology::LineStrip;
	if (topology == "pointList") return GraphicsPipeline::Topology::PointList;
	throw GardenError("Unknown pipeline topology type. (" + string(topology) + ")");
}
/**
 * @brief Returns polygon rasterization mode.
 * @param polygon target polygon rasterization mode name string (camelCase)
 * @throw GardenError on unknown polygon rasterization mode.
 */
static GraphicsPipeline::Polygon toPolygon(string_view polygon)
{
	if (polygon == "fill") return GraphicsPipeline::Polygon::Fill;
	if (polygon == "line") return GraphicsPipeline::Polygon::Line;
	if (polygon == "point") return GraphicsPipeline::Polygon::Point;
	throw GardenError("Unknown pipeline polygon type. (" + string(polygon) + ")");
}
/**
 * @brief Returns triangle culling mode.
 * @param cullFace target triangle culling mode name string (camelCase)
 * @throw GardenError on unknown triangle culling mode.
 */
static GraphicsPipeline::CullFace toCullFace(string_view cullFace)
{
	if (cullFace == "front") return GraphicsPipeline::CullFace::Front;
	if (cullFace == "back") return GraphicsPipeline::CullFace::Back;
	if (cullFace == "frontAndBack") return GraphicsPipeline::CullFace::FrontAndBack;
	throw GardenError("Unknown pipeline cull face type. (" + string(cullFace) + ")");
}
/**
 * @brief Returns polygon front-facing orientation.
 * @param frontFace target polygon front-facing orientation name string (camelCase)
 * @throw GardenError on unknown polygon front-facing orientation.
 */
static GraphicsPipeline::FrontFace toFrontFace(string_view frontFace)
{
	if (frontFace == "clockwise") return GraphicsPipeline::FrontFace::Clockwise;
	if (frontFace == "counterClockwise") return GraphicsPipeline::FrontFace::CounterClockwise;
	throw GardenError("Unknown pipeline front face type. (" + string(frontFace) + ")");
}

/***********************************************************************************************************************
 * @brief Returns framebuffer blending factor.
 * @param blendFactor target framebuffer blending factor name string (camelCase)
 * @throw GardenError on unknown framebuffer blending factor.
 */
static GraphicsPipeline::BlendFactor toBlendFactor(string_view blendFactor)
{
	if (blendFactor == "zero") return GraphicsPipeline::BlendFactor::Zero;
	if (blendFactor == "one") return GraphicsPipeline::BlendFactor::One;
	if (blendFactor == "srcColor") return GraphicsPipeline::BlendFactor::SrcColor;
	if (blendFactor == "oneMinusSrcColor") return GraphicsPipeline::BlendFactor::OneMinusSrcColor;
	if (blendFactor == "dstColor") return GraphicsPipeline::BlendFactor::DstColor;
	if (blendFactor == "oneMinusDstColor") return GraphicsPipeline::BlendFactor::OneMinusDstColor;
	if (blendFactor == "srcAlpha") return GraphicsPipeline::BlendFactor::SrcAlpha;
	if (blendFactor == "oneMinusSrcAlpha") return GraphicsPipeline::BlendFactor::OneMinusSrcAlpha;
	if (blendFactor == "dstAlpha") return GraphicsPipeline::BlendFactor::DstAlpha;
	if (blendFactor == "oneMinusDstAlpha") return GraphicsPipeline::BlendFactor::OneMinusDstAlpha;
	if (blendFactor == "constColor") return GraphicsPipeline::BlendFactor::ConstColor;
	if (blendFactor == "oneMinusConstColor") return GraphicsPipeline::BlendFactor::OneMinusConstColor;
	if (blendFactor == "constAlpha") return GraphicsPipeline::BlendFactor::ConstAlpha;
	if (blendFactor == "oneMinusConstAlpha") return GraphicsPipeline::BlendFactor::OneMinusConstAlpha;
	if (blendFactor == "src1Color") return GraphicsPipeline::BlendFactor::Src1Color;
	if (blendFactor == "oneMinusSrc1Color") return GraphicsPipeline::BlendFactor::OneMinusSrc1Color;
	if (blendFactor == "src1Alpha") return GraphicsPipeline::BlendFactor::Src1Alpha;
	if (blendFactor == "oneMinusSrc1Alpha") return GraphicsPipeline::BlendFactor::OneMinusSrc1Alpha;
	if (blendFactor == "srcAlphaSaturate") return GraphicsPipeline::BlendFactor::SrcAlphaSaturate;
	throw GardenError("Unknown pipeline blend factor type. (" + string(blendFactor) + ")");
}
/**
 * @brief Returns framebuffer blending operation.
 * @param blendOperation target framebuffer blending operation name string (camelCase)
 * @throw GardenError on unknown framebuffer blending operation.
 */
static GraphicsPipeline::BlendOperation toBlendOperation(string_view blendOperation)
{
	if (blendOperation == "add") return GraphicsPipeline::BlendOperation::Add;
	if (blendOperation == "subtract") return GraphicsPipeline::BlendOperation::Subtract;
	if (blendOperation == "reverseSubtract") return GraphicsPipeline::BlendOperation::ReverseSubtract;
	if (blendOperation == "minimum") return GraphicsPipeline::BlendOperation::Minimum;
	if (blendOperation == "maximum") return GraphicsPipeline::BlendOperation::Maximum;
	throw GardenError("Unknown pipeline blend operation type. (" + string(blendOperation) + ")");
}

/***********************************************************************************************************************
 * @brief Graphics pipeline resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class GraphicsPipelineExt final
{
public:
	/**
	 * @brief Returns graphics pipeline framebuffer color attachment count.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target graphics pipeline instance
	 */
	static uint8& getAttachmentCount(GraphicsPipeline& pipeline) { return pipeline.attachmentCount; }
	/**
	 * @brief Returns graphics pipeline parent framebuffer.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target graphics pipeline instance
	 */
	static uint8& getSubpassIndex(GraphicsPipeline& pipeline) { return pipeline.subpassIndex; }
	/**
	 * @brief Returns graphics pipeline subpass index inside framebuffer pass.
	 * @warning In most cases you should use @ref GraphicsPipeline functions.
	 * @param[in] pipeline target graphics pipeline instance
	 */
	static ID<Framebuffer>& getFramebuffer(GraphicsPipeline& pipeline) { return pipeline.framebuffer; }

	/**
	 * @brief Creates a new graphics pipeline data.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] createData target graphics pipeline create data
	 * @param useAsyncRecording use multithreaded render commands recording
	 */
	static GraphicsPipeline create(GraphicsPipeline::GraphicsCreateData& createData, bool useAsyncRecording)
	{
		return GraphicsPipeline(createData, useAsyncRecording);
	}
	/**
	 * @brief Moves internal graphics pipeline objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source graphics pipeline instance
	 * @param[in,out] destination destination graphics pipeline instance
	 */
	static void moveInternalObjects(GraphicsPipeline& source, GraphicsPipeline& destination) noexcept
	{
		GraphicsPipelineExt::getAttachmentCount(destination) = GraphicsPipelineExt::getAttachmentCount(source);
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // namespace garden::graphics
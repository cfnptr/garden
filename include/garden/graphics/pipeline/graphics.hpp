//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/graphics/pipeline.hpp"
#include "garden/graphics/framebuffer.hpp"

namespace garden::graphics
{

using namespace std;
using namespace math;
using namespace ecsm;
class GraphicsPipelineExt;

//--------------------------------------------------------------------------------------------------
class GraphicsPipeline final : public Pipeline
{
public:
	enum class Index : uint8
	{
		Uint16, Uint32, Count
	};
	enum class Topology : uint8
	{
		TriangleList, TriangleStrip, LineList, LineStrip, PointList, Count
	};
	enum class Polygon : uint8
	{
		Fill, Line, Point, Count
	};
	enum class CullFace : uint8
	{
		Front, Back, FrontAndBack, Count
	};
	enum class FrontFace : uint8
	{
		Clockwise, CounterClockwise, Count
	};
	enum class BlendFactor : uint8
	{
		Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
		SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha,
		ConstColor, OneMinusConstColor, ConstAlpha, OneMinusConstAlpha,
		Src1Color, OneMinusSrc1Color, Src1Alpha, OneMinusSrc1Alpha,
		SrcAlphaSaturate, Count
	};
	enum class BlendOperation : uint8
	{
		Add, Subtract, ReverseSubtract, Minimum, Maximum, Count
	};
	enum class ColorComponent : uint8
	{
		None = 0x00, R = 0x01, G = 0x02, B = 0x04, A = 0x08, All = 0x0F,
	};

	struct BlendState final
	{
		bool blending = false;
		BlendFactor srcColorFactor = BlendFactor::SrcAlpha;
		BlendFactor dstColorFactor = BlendFactor::OneMinusSrcAlpha;
		BlendOperation colorOperation = BlendOperation::Add;
		BlendFactor srcAlphaFactor = BlendFactor::One;
		BlendFactor dstAlphaFactor = BlendFactor::Zero;
		BlendOperation alphaOperation = BlendOperation::Add;
		ColorComponent colorMask = ColorComponent::All;
	};
	struct State final
	{
		uint8 depthTesting : 1;
		uint8 depthWriting : 1;
		uint8 depthClamping : 1;
		uint8 depthBiasing : 1;
		uint8 faceCulling : 1;
		uint8 discarding : 1;
		uint8 _unused : 1;
		Topology topology = Topology::TriangleList;
		Polygon polygon = Polygon::Fill;
		CompareOperation depthCompare = CompareOperation::Greater;
		float depthBiasConstant = 0.0f;
		float depthBiasClamp = 0.0f;
		float depthBiasSlope = 0.0f;
		float4 blendConstant = float4(0.0f);
		CullFace cullFace = CullFace::Back;
		FrontFace frontFace = FrontFace::CounterClockwise;
		uint16 _alignment = 0; // should be algined.

		State() noexcept : depthTesting(0), depthWriting(0), depthClamping(0),
			depthBiasing(0), faceCulling(1), discarding(0), _unused(0) { }
	};
	struct VertexAttribute final
	{
		GslDataType type = {};
		GslDataFormat format = {};
		uint16 offset = 0;
		// should be algined.
	};
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
		map<uint8, State> stateOverrides;
		void* renderPass = nullptr;
		State pipelineState;
		uint16 vertexAttributesSize = 0;
	};
private:
	uint8 subpassIndex = 0;
	ID<Framebuffer> framebuffer = {};
	
	GraphicsPipeline() = default;
	GraphicsPipeline(const fs::path& path, uint32 maxBindlessCount, bool useAsync,
		uint64 pipelineVersion, ID<Framebuffer> framebuffer, uint8 subpassIndex) :
		Pipeline(PipelineType::Graphics, path, maxBindlessCount, useAsync, pipelineVersion)
	{
		this->framebuffer = framebuffer;
		this->subpassIndex = subpassIndex;
	}

	GraphicsPipeline(GraphicsCreateData& createData, bool useAsync);
	// TODO: Also allow to override blendStates, vertexAttributes separatly.
	// TODO: Add dynamic and dynamic/static states (viewport/scissor).

	friend class CommandBuffer;
	friend class GraphicsPipelineExt;
	friend class LinearPool<GraphicsPipeline>;
public:
	ID<Framebuffer> getFramebuffer() const noexcept { return framebuffer; }
	uint8 getSubpassIndex() const noexcept { return (uint8)subpassIndex; }
	void updateFramebuffer(ID<Framebuffer> framebuffer);

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	void setViewport(const float4& viewport);
	void setViewportAsync(const float4& viewport, int32 taskIndex = -1);

	void setScissor(const int4& scissor);
	void setScissorAsync(const int4& scissor, int32 taskIndex = -1);

	void setViewportScissor(const float4& viewportScissor);
	void setViewportScissorAsync(const float4& viewportScissor, int32 taskIndex = -1);

	void draw(ID<Buffer> vertexBuffer, uint32 vertexCount, uint32 instanceCount = 1,
		uint32 vertexOffset = 0, uint32 instanceOffset = 0);
	void drawAsync(int32 taskIndex, ID<Buffer> vertexBuffer, uint32 vertexCount,
		uint32 instanceCount = 1, uint32 vertexOffset = 0, uint32 instanceOffset = 0);

	void drawIndexed(ID<Buffer> vertexBuffer, ID<Buffer> indexBuffer,
		Index indexType, uint32 indexCount, uint32 instanceCount = 1,
		uint32 indexOffset = 0, uint32 instanceOffset = 0, uint32 vertexOffset = 0);
	void drawIndexedAsync(int32 taskIndex, ID<Buffer> vertexBuffer,
		ID<Buffer> indexBuffer, Index indexType, uint32 indexCount,
		uint32 instanceCount = 1, uint32 indexOffset = 0,
		uint32 instanceOffset = 0, uint32 vertexOffset = 0);

	void drawFullscreen();
	void drawFullscreenAsync(int32 taskIndex);
};

DECLARE_ENUM_CLASS_FLAG_OPERATORS(GraphicsPipeline::ColorComponent)

//--------------------------------------------------------------------------------------------------
static psize toBinarySize(GraphicsPipeline::Index indexType)
{
	switch (indexType)
	{
	case GraphicsPipeline::Index::Uint16: return sizeof(uint16);
	case GraphicsPipeline::Index::Uint32: return sizeof(uint32);
	default: abort();
	}
}
static GraphicsPipeline::Topology toTopology(string_view topology)
{
	if (topology == "triangleList") return GraphicsPipeline::Topology::TriangleList;
	if (topology == "triangleStrip") return GraphicsPipeline::Topology::TriangleStrip;
	if (topology == "lineList") return GraphicsPipeline::Topology::LineList;
	if (topology == "lineStrip") return GraphicsPipeline::Topology::LineStrip;
	if (topology == "pointList") return GraphicsPipeline::Topology::PointList;
	throw runtime_error("Unknown pipeline topology type. (" + string(topology) + ")");
}
static GraphicsPipeline::Polygon toPolygon(string_view polygon)
{
	if (polygon == "fill") return GraphicsPipeline::Polygon::Fill;
	if (polygon == "line") return GraphicsPipeline::Polygon::Line;
	if (polygon == "point") return GraphicsPipeline::Polygon::Point;
	throw runtime_error("Unknown pipeline polygon type. (" + string(polygon) + ")");
}
static GraphicsPipeline::CullFace toCullFace(string_view cullFace)
{
	if (cullFace == "front") return GraphicsPipeline::CullFace::Front;
	if (cullFace == "back") return GraphicsPipeline::CullFace::Back;
	if (cullFace == "frontAndBack") return GraphicsPipeline::CullFace::FrontAndBack;
	throw runtime_error("Unknown pipeline cull face type. (" + string(cullFace) + ")");
}
static GraphicsPipeline::FrontFace toFrontFace(string_view cullFace)
{
	if (cullFace == "clockwise") return GraphicsPipeline::FrontFace::Clockwise;
	if (cullFace == "counterClockwise")
		return GraphicsPipeline::FrontFace::CounterClockwise;
	throw runtime_error("Unknown pipeline front face type. (" + string(cullFace) + ")");
}
static GraphicsPipeline::BlendFactor toBlendFactor(string_view blendFactor)
{
	if (blendFactor == "zero")
		return GraphicsPipeline::BlendFactor::Zero;
	if (blendFactor == "one")
		return GraphicsPipeline::BlendFactor::One;
	if (blendFactor == "srcColor")
		return GraphicsPipeline::BlendFactor::SrcColor;
	if (blendFactor == "oneMinusSrcColor")
		return GraphicsPipeline::BlendFactor::OneMinusSrcColor;
	if (blendFactor == "dstColor")
		return GraphicsPipeline::BlendFactor::DstColor;
	if (blendFactor == "oneMinusDstColor")
		return GraphicsPipeline::BlendFactor::OneMinusDstColor;
	if (blendFactor == "srcAlpha")
		return GraphicsPipeline::BlendFactor::SrcAlpha;
	if (blendFactor == "oneMinusSrcAlpha")
		return GraphicsPipeline::BlendFactor::OneMinusSrcAlpha;
	if (blendFactor == "dstAlpha")
		return GraphicsPipeline::BlendFactor::DstAlpha;
	if (blendFactor == "oneMinusDstAlpha")
		return GraphicsPipeline::BlendFactor::OneMinusDstAlpha;
	if (blendFactor == "constColor")
		return GraphicsPipeline::BlendFactor::ConstColor;
	if (blendFactor == "oneMinusConstColor")
		return GraphicsPipeline::BlendFactor::OneMinusConstColor;
	if (blendFactor == "constAlpha")
		return GraphicsPipeline::BlendFactor::ConstAlpha;
	if (blendFactor == "oneMinusConstAlpha")
		return GraphicsPipeline::BlendFactor::OneMinusConstAlpha;
	if (blendFactor == "src1Color")
		return GraphicsPipeline::BlendFactor::Src1Color;
	if (blendFactor == "oneMinusSrc1Color")
		return GraphicsPipeline::BlendFactor::OneMinusSrc1Color;
	if (blendFactor == "src1Alpha")
		return GraphicsPipeline::BlendFactor::Src1Alpha;
	if (blendFactor == "oneMinusSrc1Alpha")
		return GraphicsPipeline::BlendFactor::OneMinusSrc1Alpha;
	if (blendFactor == "srcAlphaSaturate")
		return GraphicsPipeline::BlendFactor::SrcAlphaSaturate;
	throw runtime_error("Unknown pipeline "
		"blend factor type. (" + string(blendFactor) + ")");
}
static GraphicsPipeline::BlendOperation toBlendOperation(string_view blendOperation)
{
	if (blendOperation == "add")
		return GraphicsPipeline::BlendOperation::Add;
	if (blendOperation == "subtract")
		return GraphicsPipeline::BlendOperation::Subtract;
	if (blendOperation == "reverseSubtract")
		return GraphicsPipeline::BlendOperation::ReverseSubtract;
	if (blendOperation == "minimum")
		return GraphicsPipeline::BlendOperation::Minimum;
	if (blendOperation == "maximum")
		return GraphicsPipeline::BlendOperation::Maximum;
	throw runtime_error("Unknown pipeline "
		"blend operation type. (" + string(blendOperation) + ")");
}

//--------------------------------------------------------------------------------------------------
class GraphicsPipelineExt final
{
public:
	static GraphicsPipeline create(
		GraphicsPipeline::GraphicsCreateData& createData, bool useAsync)
	{
		return GraphicsPipeline(createData, useAsync);
	}
	static void moveInternalObjects(GraphicsPipeline& source,
		GraphicsPipeline& destination) noexcept
	{
		PipelineExt::moveInternalObjects(source, destination);
	}
};

} // garden::graphics
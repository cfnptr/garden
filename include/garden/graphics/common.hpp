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
#include "garden/defines.hpp"
#include "math/color.hpp"
#include "math/flags.hpp"

#include <string>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace garden::graphics
{

#define GARDEN_FRAME_LAG 2 // Optimal count
#define GARDEN_MAX_PUSH_CONSTANTS_SIZE 128 // Nvidia maximum

using namespace std;
using namespace math;

//--------------------------------------------------------------------------------------------------
enum class ShaderStage : uint8
{
	None = 0x00, Vertex = 0x01, Fragment = 0x02, Compute = 0x04
};
enum class PipelineType : uint8
{
	Graphics, Compute, Count // TODO: ray tracing
};
enum class SamplerFilter : uint8
{
	Nearest, Linear, Count,
};

// All command buffer types may have several frames delay.
enum class CommandBufferType : uint8
{
	Frame, // Current frame command buffer.
	Graphics, // Supports graphics, transfer and compute commands.
	TransferOnly, // Supports transfer only commands.
	ComputeOnly, // Supports compute only commands.
	AsyncCompute, // Async compute command buffer, same as compute.
	Count
};

#define SHADER_STAGE_COUNT 3
DECLARE_ENUM_CLASS_FLAG_OPERATORS(ShaderStage)

//--------------------------------------------------------------------------------------------------
static SamplerFilter toSamplerFilter(string_view samplerFilter)
{
	if (samplerFilter == "nearest") return SamplerFilter::Nearest;
	if (samplerFilter == "linear") return SamplerFilter::Linear;
	throw runtime_error("Unknown sampler filter type. (" + string(samplerFilter) + ")");
}

//--------------------------------------------------------------------------------------------------
static const string_view samplerFilterNames[(psize)SamplerFilter::Count] =
{
	"Nearest", "Linear"
};

static string_view toString(ShaderStage shaderStage)
{
	if (hasOneFlag(shaderStage, ShaderStage::None)) return "None";
	if (hasOneFlag(shaderStage, ShaderStage::Vertex)) return "Vertex";
	if (hasOneFlag(shaderStage, ShaderStage::Fragment)) return "Fragment";
	if (hasOneFlag(shaderStage, ShaderStage::Compute)) return "Compute";
	throw runtime_error("Unknown shader stage type. (" + to_string((int)shaderStage) + ")");
}
static string toStringList(ShaderStage shaderStage)
{
	string list;
	if (hasAnyFlag(shaderStage, ShaderStage::None)) list += "None | ";
	if (hasAnyFlag(shaderStage, ShaderStage::Vertex)) list += "Vertex | ";
	if (hasAnyFlag(shaderStage, ShaderStage::Fragment)) list += "Fragment | ";
	if (hasAnyFlag(shaderStage, ShaderStage::Compute)) list += "Compute | ";
	list.resize(list.length() - 3);
	return list;
}
static string_view toString(SamplerFilter samplerFilter)
{
	GARDEN_ASSERT((uint8)samplerFilter < (uint8)SamplerFilter::Count);
	return samplerFilterNames[(psize)samplerFilter];
}

#if GARDEN_DEBUG
struct DebugLabel
{
	static void begin(const string& name, const Color& color);
	static void end();
	static void insert(const string& name, const Color& color);

	// TODO: add suport for the vulkan queue labels.
	DebugLabel(const string& name, const Color& color) { begin(name, color); }
	~DebugLabel() { end(); }
};

#define SET_GPU_DEBUG_LABEL(name, color) DebugLabel _debugLabel(name, color)
#define INSERT_GPU_DEBUG_LABEL(name, color) DebugLabel::insert(name, color)
#define BEGIN_GPU_DEBUG_LABEL(name, color) DebugLabel::begin(name, color)
#define END_GPU_DEBUG_LABEL() DebugLabel::end()
#else
#define SET_GPU_DEBUG_LABEL(name, color)
#define INSERT_GPU_DEBUG_LABEL(name, color)
#define BEGIN_GPU_DEBUG_LABEL(name, color)
#define END_GPU_DEBUG_LABEL(name, color)
#endif

} // garden::graphics
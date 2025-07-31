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

// TODO: Refactor this dumpster fire. Use proper C tokenizer and transpiler.
//       Or even better fork glslc compiler and integrate this into it.

#include "garden/graphics/gslc.hpp"
#include "garden/thread-pool.hpp"
#include "garden/file.hpp"

#include <atomic>
#include <cmath>
#include <exception>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

#if GARDEN_OS_WINDOWS
#include <windows.h>
#endif

using namespace garden;
using namespace garden::graphics;

//******************************************************************************************************************
constexpr uint8 gslHeader[] = { 1, 0, 0, GARDEN_LITTLE_ENDIAN, };

namespace garden::graphics
{
	struct GslValues
	{
		uint8 uniformCount = 0;
		uint8 samplerStateCount = 0;
		uint8 descriptorSetCount = 0;
		uint8 variantCount = 0;
		uint16 pushConstantsSize = 0;
		uint8 specConstCount = 0;
	};
	struct GraphicsGslValues final : public GslValues
	{
		uint8 _alignment = 0;
		uint8 vertexAttributeCount = 0;
		uint8 blendStateCount = 0;
		uint16 vertexAttributesSize = 0;
		ShaderStage pushConstantsStages = {};
		GraphicsPipeline::State pipelineState = {};
		// Note: Should be aligned.
	};
	struct ComputeGslValues final : public GslValues
	{
		uint8 _alignment = 0;
		uint3 localSize = uint3::zero;
		// Note: Should be aligned.
	};
	struct RayTracingGslValues final : public GslValues
	{
		uint8 _alignment = 0;
		ShaderStage pushConstantsStages = {};
		uint32 rayRecursionDepth = 0;
		// Note: Should be aligned.
	};
}

#if !GARDEN_PACK_RESOURCES || defined(GSL_COMPILER)
//******************************************************************************************************************
namespace garden::graphics
{
	struct FileData
	{
		ifstream inputFileStream; ofstream outputFileStream;
		istringstream inputStream; ostringstream outputStream;
		string line; uint32 lineIndex = 1;
		int8 isUniform = 0, isBuffer = 0, isPushConstants = 0, isSamplerState = 0;
		bool isSkipMode = false, isReadonly = false, isWriteonly = false, isMutable = false, 
			isRestrict = false, isVolatile = false, isCoherent = false, isScalar = false;
		uint8 attachmentIndex = 0, descriptorSetIndex = 0, specConstIndex = 1;
		GslUniformType uniformType = {};
		Sampler::State samplerState;
	};
	struct GraphicsFileData final : public FileData
	{
		int8 isPipelineState = 0;
		uint8 inIndex = 0, outIndex = 0, blendStateIndex = 0;
	};
	struct ComputeFileData final : public FileData
	{
		uint8 bindingIndex = 0;
	};
	struct RayTracingFileData final : public FileData
	{
		uint8 rayPayloadIndex = 0, callableDataIndex = 0;
	};

	struct LineData
	{
		string word, uniformName;
		int8 isSpecConst = 0;
		bool isComparing = false, isCompareOperation = false, isAnisoFiltering = false, isMaxAnisotropy = false, 
			isUnnormCoords = false, isMipLodBias = false, isMinLod = false, isMaxLod = false, isFilter = false, 
			isFilterMin = false, isFilterMag = false, isFilterMipmap = false, isBorderColor = false, 
			isAddressMode = false, isAddressModeX = false, isAddressModeY = false, isAddressModeZ = false,
			isFeature = false, isVariantCount = false, isReference = false;
		uint8 arraySize = 1;
		GslDataType dataType = {}; Image::Format imageFormat = {};
		bool isNewLine = true;
	};
	struct GraphicsLineData final : public LineData
	{
		int8 isIn = 0, isOut = 0, isTopology = 0, isPolygon = 0, isDiscarding = 0,
			isDepthTesting = 0, isDepthWriting = 0, isDepthClamping = 0,
			isDepthBiasing = 0, isDepthCompare = 0, isDepthOverride = 0,
			isStencilTesting = 0, isFaceCulling = 0, isCullFace = 0, isFrontFace = 0,
			isBlending = 0, isColorMask = 0, isSrcBlendFactor = 0,
			isDstBlendFactor = 0, isSrcColorFactor = 0,isDstColorFactor = 0,
			isSrcAlphaFactor = 0, isDstAlphaFactor = 0, isBlendOperation = 0,
			isColorOperation = 0, isAlphaOperation = 0,
			isAttributeOffset = 0, isAttachmentOffset = 0;
		bool isFlat = false, isNoperspective = false;
	};
	struct ComputeLineData final : public LineData
	{
		int8 isLocalSize = 0;
	};
	struct RayTracingLineData final : public LineData
	{
		int8 isRayPayload = 0, isRayPayloadIn = 0, isCallableData = 0, isCallableDataIn = 0, 
			isRayPayloadOffset = 0, isCallableDataOffset = 0, isRayRecursionDepth = 0;
	};

	class CompileError final : public GardenError
	{
		string errorMessage, word;
		int32 line = -1;
	public:
		CompileError(const string& errorMessage, int32 line = -1, const string& word = "") noexcept :
			errorMessage(errorMessage), word(word), line(line)
		{
			message = line < 0 ? "error:" + (word.length() > 0 ? "'" + word + "' : " : " ") + errorMessage :
				to_string(line) + ": error:" + (word.length() > 0 ? "'" + word + "' : " : " ") + errorMessage;
		}

		const string& getErrorMessage() const noexcept { return errorMessage; }
		const string& getWord() const noexcept { return word; }
		int32 getLine() const noexcept { return line; }
	};
}

//******************************************************************************************************************
static bool toBoolState(string_view state, uint32 lineIndex)
{
	if (state == "on") return true;
	if (state == "off") return false;
	throw CompileError("unrecognized boolean state", lineIndex, string(state));
}
static string_view toStringState(bool state) noexcept
{
	return state ? "on" : "off";
}

static float toFloatValue(string_view value, uint32 lineIndex)
{
	if (value == "inf") return INFINITY;
	try { return stof(string(value)); }
	catch (const exception&)
	{ throw CompileError("invalid floating point value", lineIndex, string(value)); }
}

static GraphicsPipeline::ColorComponent toColorComponents(string_view colorComponents) noexcept
{
	if (colorComponents == "all") return GraphicsPipeline::ColorComponent::All;
	if (colorComponents == "none") return GraphicsPipeline::ColorComponent::None;

	auto components = GraphicsPipeline::ColorComponent::None;
	for (auto letter : colorComponents)
	{
		if (letter == 'r') components |= GraphicsPipeline::ColorComponent::R;
		else if (letter == 'g') components |= GraphicsPipeline::ColorComponent::G;
		else if (letter == 'b') components |= GraphicsPipeline::ColorComponent::B;
		else if (letter == 'a') components |= GraphicsPipeline::ColorComponent::A;
	}
	return components;
}

static Image::Format toImageFormat(string_view gslImageFormat)
{
	if (gslImageFormat == "uintR8") return Image::Format::UintR8;
	if (gslImageFormat == "uintR8G8") return Image::Format::UintR8G8;
	if (gslImageFormat == "uintR8G8B8A8") return Image::Format::UintR8G8B8A8;
	if (gslImageFormat == "uintR16") return Image::Format::UintR16;
	if (gslImageFormat == "uintR16G16") return Image::Format::UintR16G16;
	if (gslImageFormat == "uintR16G16B16A16") return Image::Format::UintR16G16B16A16;
	if (gslImageFormat == "uintR32") return Image::Format::UintR32;
	if (gslImageFormat == "uintR32G32") return Image::Format::UintR32G32;
	if (gslImageFormat == "uintR32G32B32A32") return Image::Format::UintR32G32B32A32;
	if (gslImageFormat == "uintA2R10G10B10") return Image::Format::UintA2R10G10B10;

	if (gslImageFormat == "sintR8") return Image::Format::SintR8;
	if (gslImageFormat == "sintR8G8") return Image::Format::SintR8G8;
	if (gslImageFormat == "sintR8G8B8A8") return Image::Format::SintR8G8B8A8;
	if (gslImageFormat == "sintR16") return Image::Format::SintR16;
	if (gslImageFormat == "sintR16G16") return Image::Format::SintR16G16;
	if (gslImageFormat == "sintR16G16B16A16") return Image::Format::SintR16G16B16A16;
	if (gslImageFormat == "sintR32") return Image::Format::SintR32;
	if (gslImageFormat == "sintR32G32") return Image::Format::SintR32G32;
	if (gslImageFormat == "sintR32G32B32A32") return Image::Format::SintR32G32B32A32;

	if (gslImageFormat == "unormR8") return Image::Format::UnormR8;
	if (gslImageFormat == "unormR8G8") return Image::Format::UnormR8G8;
	if (gslImageFormat == "unormR8G8B8A8") return Image::Format::UnormR8G8B8A8;
	if (gslImageFormat == "unormR16") return Image::Format::UnormR16;
	if (gslImageFormat == "unormR16G16") return Image::Format::UnormR16G16;
	if (gslImageFormat == "unormR16G16B16A16") return Image::Format::UnormR16G16B16A16;
	if (gslImageFormat == "unormA2R10G10B10") return Image::Format::UnormA2R10G10B10;
	
	if (gslImageFormat == "snormR8") return Image::Format::SnormR8;
	if (gslImageFormat == "snormR8G8") return Image::Format::SnormR8G8;
	if (gslImageFormat == "snormR8G8B8A8") return Image::Format::SnormR8G8B8A8;
	if (gslImageFormat == "snormR16") return Image::Format::SnormR16;
	if (gslImageFormat == "snormR16G16") return Image::Format::SnormR16G16;
	if (gslImageFormat == "snormR16G16B16A16") return Image::Format::SnormR16G16B16A16;

	if (gslImageFormat == "sfloatR16") return Image::Format::SfloatR16;
	if (gslImageFormat == "sfloatR16G16") return Image::Format::SfloatR16G16;
	if (gslImageFormat == "sfloatR16G16B16A16") return Image::Format::SfloatR16G16B16A16;
	if (gslImageFormat == "sfloatR32") return Image::Format::SfloatR32;
	if (gslImageFormat == "sfloatR32G32") return Image::Format::SfloatR32G32;
	if (gslImageFormat == "sfloatR32G32B32A32") return Image::Format::SfloatR32G32B32A32;

	if (gslImageFormat == "ufloatB10G11R11") return Image::Format::UfloatB10G11R11;

	return Image::Format::Undefined;
}
static string_view toGlslString(Image::Format imageFormat)
{
	switch (imageFormat)
	{
	case Image::Format::UintR8: return "r8ui";
	case Image::Format::UintR8G8: return "rg8ui";
	case Image::Format::UintR8G8B8A8: return "rgba8ui";
	case Image::Format::UintR16: return "r16ui";
	case Image::Format::UintR16G16: return "rg16ui";
	case Image::Format::UintR16G16B16A16: return "rgba16ui";
	case Image::Format::UintR32: return "r32ui";
	case Image::Format::UintR32G32: return "rg32ui";
	case Image::Format::UintR32G32B32A32: return "rgba32ui";
	case Image::Format::UintA2R10G10B10: return "rgb10_a2ui";

	case Image::Format::SintR8: return "r8i";
	case Image::Format::SintR8G8: return "rg8i";
	case Image::Format::SintR8G8B8A8: return "rgba8i";
	case Image::Format::SintR16: return "r16i";
	case Image::Format::SintR16G16: return "rg16i";
	case Image::Format::SintR16G16B16A16: return "rgba16i";
	case Image::Format::SintR32: return "r32i";
	case Image::Format::SintR32G32: return "rg32i";
	case Image::Format::SintR32G32B32A32: return "rgba32i";

	case Image::Format::UnormR8: return "r8";
	case Image::Format::UnormR8G8: return "rg8";
	case Image::Format::UnormR8G8B8A8: return "rgba8";
	case Image::Format::UnormR16: return "r16";
	case Image::Format::UnormR16G16: return "rg16";
	case Image::Format::UnormR16G16B16A16: return "rgba16";
	case Image::Format::UnormA2R10G10B10: return "rgb10_a2";

	case Image::Format::SnormR8: return "r8_snorm";
	case Image::Format::SnormR8G8: return "rg8_snorm";
	case Image::Format::SnormR8G8B8A8: return "rgba8_snorm";
	case Image::Format::SnormR16: return "r16_snorm";
	case Image::Format::SnormR16G16: return "rg16_snorm";
	case Image::Format::SnormR16G16B16A16: return "rgba16_snorm";

	case Image::Format::SfloatR16: return "r16f";
	case Image::Format::SfloatR16G16: return "rg16f";
	case Image::Format::SfloatR16G16B16A16: return "rgba16f";
	case Image::Format::SfloatR32: return "r32f";
	case Image::Format::SfloatR32G32: return "rg32f";
	case Image::Format::SfloatR32G32B32A32: return "rgba32f";

	case Image::Format::UfloatB10G11R11: return "r11f_g11f_b10f"; // Yeah, it's inverted.

	default: abort();
	}
}

//******************************************************************************************************************
static void endShaderUniform(FileData& fileData)
{
	fileData.isReadonly = fileData.isWriteonly = fileData.isMutable = fileData.isRestrict = 
		fileData.isVolatile = fileData.isCoherent = fileData.isScalar = false;
	fileData.isUniform = fileData.descriptorSetIndex = 0;
}

static void onShaderUniform(FileData& fileData, LineData& lineData, ShaderStage shaderStage, 
	uint8& bindingIndex, Pipeline::Uniforms& uniforms, Pipeline::SamplerStates& samplerStates)
{
	if (fileData.isUniform == 1)
	{
		if (lineData.word == "readonly") fileData.isReadonly = true;
		else if (lineData.word == "writeonly") fileData.isWriteonly = true;
		else if (lineData.word == "mutable") fileData.isMutable = true;
		else if (lineData.word == "restrict") fileData.isRestrict = true;
		else if (lineData.word == "volatile") fileData.isVolatile = true;
		else if (lineData.word == "coherent") fileData.isCoherent = true;
		else if (lineData.word == "scalar") fileData.isScalar = true;
		else if (lineData.word == "reference") lineData.isReference = true;
		else if (lineData.word.length() > 3 && memcmp(lineData.word.data(), "set", 3) == 0) // Note: Do not move down.
		{
			auto index = strtoul(lineData.word.c_str() + 3, nullptr, 10);
			if (index > UINT8_MAX)
				throw CompileError("invalid descriptor set index", fileData.lineIndex, lineData.word);
			fileData.descriptorSetIndex = (uint8)index;
		}
		else if (fileData.isBuffer)
		{
			if (lineData.isReference)
			{
				fileData.outputFileStream << "layout(buffer_reference";
				if (fileData.isScalar) fileData.outputFileStream << ", scalar";
				else fileData.outputFileStream << ", std430";
				fileData.outputFileStream << ") ";
				if (fileData.isReadonly) fileData.outputFileStream << "readonly ";
				if (fileData.isWriteonly) fileData.outputFileStream << "writeonly ";
				if (fileData.isRestrict) fileData.outputFileStream << "restrict ";
				if (fileData.isVolatile) fileData.outputFileStream << "volatile ";
				if (fileData.isCoherent) fileData.outputFileStream << "coherent ";
				fileData.outputFileStream << "buffer " << lineData.word;
				endShaderUniform(fileData);
			}
			else
			{
				fileData.uniformType = GslUniformType::StorageBuffer;
				fileData.outputStream.str(""); fileData.outputStream.clear();
				fileData.outputStream << lineData.word << " ";
				fileData.isBuffer = 0; fileData.isUniform = 4;
			}
		}
		else if (lineData.word == toString(GslUniformType::PushConstants))
		{
			fileData.outputFileStream << "layout(push_constant";
			if (fileData.isScalar)
			{
				fileData.outputFileStream << ", scalar";
				fileData.isScalar = false;
			}
			else fileData.outputFileStream << ", std430";
			fileData.outputFileStream << ") uniform pushConstants ";
			fileData.isUniform = 0; fileData.isPushConstants = 1;
		}
		else if (lineData.word == toString(GslUniformType::SubpassInput))
		{
			fileData.uniformType = GslUniformType::SubpassInput; fileData.isUniform = 3;
		}
		else
		{
			try
			{
				fileData.uniformType = toGslUniformType(lineData.word);
				if (isSamplerType(fileData.uniformType)) fileData.isUniform = 2;
				else if (isImageType(fileData.uniformType)) fileData.isUniform = 5;
				else fileData.isUniform = 3;
			}
			catch (const exception&)
			{
				fileData.uniformType = GslUniformType::UniformBuffer;
				fileData.outputStream.str(""); fileData.outputStream.clear();
				fileData.outputStream << lineData.word << " ";
				fileData.isUniform = 4;
			}
		}

		return;
	}
	else if (fileData.isUniform == 2)
	{
		if (lineData.word == "{")
		{
			fileData.outputStream.str(""); fileData.outputStream.clear();
			fileData.outputStream << "\n// {\n";
			fileData.isUniform = 0; fileData.isSamplerState = 1;
			return;
		}
	}
	else if (fileData.isUniform == 4)
	{
		if (lineData.word == "}") fileData.isUniform = 8;
		if (isBufferType(fileData.uniformType))
		{
			if (lineData.isNewLine) fileData.outputStream << "\n";
			fileData.outputStream << lineData.word << " ";
		}
		else fileData.outputFileStream << lineData.word << " ";
		return;
	}
	else if (fileData.isUniform == 5)
	{
		lineData.uniformName = string(lineData.word, 0, lineData.word.find_first_of(';'));
		if (lineData.word.length() != lineData.uniformName.length())
			throw CompileError("no uniform image format after name", fileData.lineIndex, lineData.uniformName);

		auto arrayOpenPos = lineData.uniformName.find_first_of('[');
		if (arrayOpenPos != string::npos)
		{
			auto arraySize = strtoul(lineData.uniformName.c_str() + arrayOpenPos + 1, nullptr, 10);
			if (arraySize == 0)
			{
				auto arrayClosePos = lineData.uniformName.find_first_of(']', arrayOpenPos);
				if (arrayClosePos == string::npos)
					throw CompileError("no ']' after uniform array size", fileData.lineIndex, lineData.uniformName);
				if (arrayClosePos - arrayOpenPos != 1)
					throw CompileError("invalid uniform array size",fileData.lineIndex, lineData.uniformName);
			}
			lineData.uniformName.resize(arrayOpenPos); lineData.arraySize = arraySize;
		}
		fileData.isUniform = 6;
		return;
	}
	else if (fileData.isUniform == 6)
	{
		if (lineData.word != ":")
			throw CompileError("no ':' after uniform image format", fileData.lineIndex, lineData.word);
		fileData.isUniform = 7;
		return;
	}
	else if (fileData.isUniform == 7)
	{
		auto format = string_view(lineData.word.c_str(), lineData.word.find_first_of(';'));
		lineData.imageFormat = toImageFormat(format);
		if (lineData.imageFormat == Image::Format::Undefined)
			throw CompileError("unrecognized uniform image format", fileData.lineIndex, string(format));
	}

	if (lineData.uniformName.empty())
	{
		lineData.uniformName = string(lineData.word, 0, lineData.word.find_first_of(';'));
		if (lineData.word.length() == lineData.uniformName.length())
			throw CompileError("no ';' after uniform name", fileData.lineIndex, lineData.uniformName);

		auto arrayOpenPos = lineData.uniformName.find_first_of('[');
		if (arrayOpenPos != string::npos)
		{
			auto arraySize = strtoul(lineData.uniformName.c_str() + arrayOpenPos + 1, nullptr, 10);
			if (arraySize == 0)
			{
				auto arrayClosePos = lineData.uniformName.find_first_of(']', arrayOpenPos);
				if (arrayClosePos == string::npos)
					throw CompileError("no ']' after uniform array size", fileData.lineIndex, lineData.uniformName);
				if (arrayClosePos - arrayOpenPos != 1)
					throw CompileError("invalid uniform array size", fileData.lineIndex, lineData.uniformName);
			}
			lineData.uniformName.resize(arrayOpenPos); lineData.arraySize = arraySize;
		}
	}

	auto writeAccess = !fileData.isReadonly && !isSamplerType(fileData.uniformType) &&
		fileData.uniformType != GslUniformType::SubpassInput &&
		fileData.uniformType != GslUniformType::UniformBuffer &&
		fileData.uniformType != GslUniformType::PushConstants &&
		fileData.uniformType != GslUniformType::AccelerationStructure;
	auto readAccess = !fileData.isWriteonly;

	if (!readAccess && !writeAccess)
		throw CompileError("no uniform read and write access", fileData.lineIndex, lineData.uniformName);

	uint8 binding;
	if (uniforms.find(lineData.uniformName) == uniforms.end())
	{
		Pipeline::Uniform uniform;
		uniform.type = fileData.uniformType;
		uniform.shaderStages = shaderStage;
		uniform.bindingIndex = binding = bindingIndex++;
		uniform.descriptorSetIndex = fileData.descriptorSetIndex;
		uniform.arraySize = lineData.arraySize;
		uniform.readAccess = readAccess;
		uniform.writeAccess = writeAccess;
		uniform.isMutable = fileData.isMutable;

		if (!uniforms.emplace(lineData.uniformName, uniform).second)
			throw GardenError("Failed to emplace uniform.");
	}
	else
	{
		auto& uniform = uniforms.at(lineData.uniformName);
		if (fileData.uniformType != uniform.type)
		{
			throw CompileError("different uniform type with the same name",
				fileData.lineIndex, to_string(uniform.descriptorSetIndex));
		}
		if (fileData.descriptorSetIndex != uniform.descriptorSetIndex)
		{
			throw CompileError("different uniform descriptor set index with the same name",
				fileData.lineIndex, to_string(uniform.descriptorSetIndex));
		}
		if (lineData.arraySize != uniform.arraySize)
		{
			throw CompileError("different uniform array size with the same name",
				fileData.lineIndex, to_string(uniform.descriptorSetIndex));
		}
		if (uniform.readAccess != readAccess || uniform.writeAccess != writeAccess)
		{
			throw CompileError("different access between "
				"stages is not supported yet", fileData.lineIndex); // TODO: add support
		}
		if (fileData.isMutable != uniform.isMutable)
		{
			throw CompileError("different mutable state between "
				"stages is not supported yet", fileData.lineIndex); // TODO: add support
		}
		uniform.shaderStages |= shaderStage; binding = uniform.bindingIndex;
	}
	
	if (fileData.uniformType == GslUniformType::SubpassInput)
	{
		fileData.outputFileStream << "layout(binding = " << to_string(binding) <<
			", set = " << to_string(fileData.descriptorSetIndex) <<
			", input_attachment_index = " << to_string(fileData.attachmentIndex++) <<
			") uniform subpassInput ";
	}
	else if (fileData.uniformType == GslUniformType::UniformBuffer)
	{
		fileData.outputFileStream << "layout(binding = " << to_string(binding) <<
			", set = " << to_string(fileData.descriptorSetIndex);
		if (fileData.isScalar) fileData.outputFileStream << ", scalar";
		fileData.outputFileStream << ") uniform " << fileData.outputStream.str();
	}
	else if (fileData.uniformType == GslUniformType::StorageBuffer)
	{
		fileData.outputFileStream << "layout(binding = " << to_string(binding) <<
			", set = " << to_string(fileData.descriptorSetIndex);
		if (fileData.isScalar) fileData.outputFileStream << ", scalar";
		else fileData.outputFileStream << ", std430";
		fileData.outputFileStream << ") ";
		if (fileData.isReadonly) fileData.outputFileStream << "readonly ";
		if (fileData.isWriteonly) fileData.outputFileStream << "writeonly ";
		if (fileData.isRestrict) fileData.outputFileStream << "restrict ";
		if (fileData.isVolatile) fileData.outputFileStream << "volatile ";
		if (fileData.isCoherent) fileData.outputFileStream << "coherent ";
		fileData.outputFileStream << "buffer " << fileData.outputStream.str();
	}
	else
	{
		fileData.outputFileStream << "layout(binding = " << to_string(binding) <<
			", set = " << to_string(fileData.descriptorSetIndex);
		if (isImageType(fileData.uniformType))
			fileData.outputFileStream << ", " << toGlslString(lineData.imageFormat);
		fileData.outputFileStream << ") ";
		if (fileData.isReadonly) fileData.outputFileStream << "readonly ";
		if (fileData.isWriteonly) fileData.outputFileStream << "writeonly ";
		if (fileData.isRestrict) fileData.outputFileStream << "restrict ";
		if (fileData.isVolatile) fileData.outputFileStream << "volatile ";
		if (fileData.isCoherent) fileData.outputFileStream << "coherent ";
		fileData.outputFileStream << "uniform " << toString(fileData.uniformType) << " ";
	}

	fileData.outputFileStream << lineData.uniformName;

	if (lineData.arraySize != 1)
	{
		if (lineData.arraySize > 0) fileData.outputFileStream << "[" << (int)lineData.arraySize << "]";
		else fileData.outputFileStream << "[]";
	}

	fileData.outputFileStream << "; ";

	if (isSamplerType(fileData.uniformType))
	{
		auto result = samplerStates.find(lineData.uniformName);
		if (result == samplerStates.end())
			samplerStates.emplace(lineData.uniformName, fileData.samplerState);
		else
		{
			if (memcmp(&fileData.samplerState, &result->second, sizeof(Sampler::State)) != 0)
			{
				throw CompileError("different sampler state with the same name",
					fileData.lineIndex, lineData.uniformName);
			}
		}

		if (fileData.isUniform == 3)
		{
			fileData.outputFileStream << fileData.outputStream.str(); fileData.samplerState = {};
		}
	}

	endShaderUniform(fileData);
}

//******************************************************************************************************************
static void onShaderPushConstants(FileData& fileData, LineData& lineData, uint16& pushConstantsSize)
{
	if (fileData.isPushConstants == 1)
	{
		if (lineData.word != "{")
			throw CompileError("undeclared push constant variables", fileData.lineIndex);
		if (pushConstantsSize > 0)
			throw CompileError("second push constants declaration", fileData.lineIndex);
		fileData.isPushConstants = 2;
	}
	else if (fileData.isPushConstants == 2)
	{
		if (lineData.word == "}") fileData.isPushConstants = 0;
		else
		{
			try { lineData.dataType = toGslDataType(lineData.word); }
			catch (const exception&)
			{ throw CompileError("unrecognized push constant GSL data type", fileData.lineIndex, lineData.word); }
			fileData.isPushConstants = 3;
		}
	}
	else
	{
		pushConstantsSize += (uint16)toBinarySize(lineData.dataType);
		fileData.isPushConstants = 2;
		if (pushConstantsSize > maxPushConstantsSize)
			throw CompileError("out of max push constants size", fileData.lineIndex);
	}
}

//******************************************************************************************************************
static Sampler::Filter toSamplerFilter(string_view name, uint32 lineIndex)
{
	try { return toSamplerFilter(name); }
	catch (const exception&) { throw CompileError("unrecognized sampler filter type", lineIndex, string(name)); }
}
static Sampler::AddressMode toAddressMode(string_view name, uint32 lineIndex)
{
	try { return toAddressMode(name); }
	catch (const exception&)
	{ throw CompileError("unrecognized sampler address mode", lineIndex, string(name)); }
}
static Sampler::BorderColor toBorderColor(string_view name, uint32 lineIndex)
{
	try { return toBorderColor(name); }
	catch (const exception&)
	{ throw CompileError("unrecognized border color type", lineIndex, string(name)); }
}
static Sampler::CompareOp toCompareOperation(string_view name, uint32 lineIndex)
{
	try { return toCompareOperation(name); }
	catch (const exception&)
	{ throw CompileError("unrecognized compare operation type", lineIndex, string(name)); }
}

//******************************************************************************************************************
static void onShaderSamplerState(FileData& fileData, LineData& lineData)
{
	if (fileData.isSamplerState == 1)
	{
		if (lineData.word == "}")
		{
			fileData.outputStream << "// }";
			fileData.isSamplerState = 0; fileData.isUniform = 3;
		}
		else
		{
			if (lineData.word == "filter") lineData.isFilter = true;
			else if (lineData.word == "filterMin") lineData.isFilterMin = true;
			else if (lineData.word == "filterMag") lineData.isFilterMag = true;
			else if (lineData.word == "filterMipmap") lineData.isFilterMipmap = true;
			else if (lineData.word == "borderColor") lineData.isBorderColor = true;
			else if (lineData.word == "addressMode") lineData.isAddressMode = true;
			else if (lineData.word == "addressModeX") lineData.isAddressModeX = true;
			else if (lineData.word == "addressModeY") lineData.isAddressModeY = true;
			else if (lineData.word == "addressModeZ") lineData.isAddressModeZ = true;
			else if (lineData.word == "comparison") lineData.isComparing = true;
			else if (lineData.word == "compareOperation") lineData.isCompareOperation = true;
			else if (lineData.word == "anisoFiltering") lineData.isAnisoFiltering = true;
			else if (lineData.word == "maxAnisotropy") lineData.isMaxAnisotropy = true;
			else if (lineData.word == "unnormCoords") lineData.isUnnormCoords = true;
			else if (lineData.word == "mipLodBias") lineData.isMipLodBias = true;
			else if (lineData.word == "minLod") lineData.isMinLod = true;
			else if (lineData.word == "maxLod") lineData.isMaxLod = true;
			else throw CompileError("unrecognized sampler property", fileData.lineIndex, lineData.word);
			fileData.outputStream << "//     " << lineData.word << " ";
			fileData.isSamplerState = 2; 
		}
	}
	else if (fileData.isSamplerState == 2)
	{
		if (lineData.word != "=")
			throw CompileError("no '=' after sampler property name", fileData.lineIndex);
		fileData.outputStream << "= "; fileData.isSamplerState = 3;
	}
	else
	{
		auto name = string_view(lineData.word.c_str(), lineData.word.find_first_of(';'));
		if (lineData.word.length() == name.length())
			throw CompileError("no ';' after sampler property name", fileData.lineIndex, string(name));

		if (lineData.isFilter)
		{
			fileData.samplerState.setFilter(toSamplerFilter(name, fileData.lineIndex));
			lineData.isFilter = false;
		}
		else if (lineData.isFilterMin)
		{
			fileData.samplerState.minFilter = toSamplerFilter(name, fileData.lineIndex);
			lineData.isFilterMin = false;
		}
		else if (lineData.isFilterMag)
		{
			fileData.samplerState.magFilter = toSamplerFilter(name,fileData.lineIndex);
			lineData.isFilterMag = false;
		}
		else if (lineData.isFilterMipmap)
		{
			fileData.samplerState.mipmapFilter = toSamplerFilter(name, fileData.lineIndex);
			lineData.isFilterMag = false;
		}
		else if (lineData.isBorderColor)
		{
			fileData.samplerState.borderColor = toBorderColor(name, fileData.lineIndex);
			lineData.isBorderColor = false;
		}
		else if (lineData.isAddressMode)
		{
			fileData.samplerState.setAddressMode(toAddressMode(name, fileData.lineIndex));
			lineData.isAddressMode = false;
		}
		else if (lineData.isAddressModeX)
		{
			fileData.samplerState.addressModeX = toAddressMode(name, fileData.lineIndex);
			lineData.isAddressModeX = false;
		}
		else if (lineData.isAddressModeY)
		{
			fileData.samplerState.addressModeY = toAddressMode(name, fileData.lineIndex);
			lineData.isAddressModeY = false;
		}
		else if (lineData.isAddressModeZ)
		{
			fileData.samplerState.addressModeZ = toAddressMode(name, fileData.lineIndex);
			lineData.isAddressModeZ = false;
		}
		else if (lineData.isComparing)
		{
			fileData.samplerState.comparison = toBoolState(name, fileData.lineIndex);
			lineData.isComparing = false;
		}
		else if (lineData.isCompareOperation)
		{
			fileData.samplerState.compareOperation = toCompareOperation(name, fileData.lineIndex);
			lineData.isCompareOperation = false;
		}
		else if (lineData.isAnisoFiltering)
		{
			fileData.samplerState.anisoFiltering = toBoolState(name, fileData.lineIndex);
			lineData.isAnisoFiltering = false;
		}
		else if (lineData.isMaxAnisotropy)
		{
			fileData.samplerState.maxAnisotropy = toFloatValue(name, fileData.lineIndex);
			lineData.isMaxAnisotropy = false;
		}
		else if (lineData.isUnnormCoords)
		{
			fileData.samplerState.unnormCoords = toBoolState(name, fileData.lineIndex);
			lineData.isUnnormCoords = false;
		}
		else if (lineData.isMipLodBias)
		{
			fileData.samplerState.mipLodBias = toFloatValue(name, fileData.lineIndex);
			lineData.isMipLodBias = false;
		}
		else if (lineData.isMinLod)
		{
			fileData.samplerState.minLod = toFloatValue(name, fileData.lineIndex);
			lineData.isMinLod = false;
		}
		else if (lineData.isMaxLod)
		{
			fileData.samplerState.maxLod = toFloatValue(name, fileData.lineIndex);
			lineData.isMaxLod = false;
		}
		else abort();

		fileData.outputStream << lineData.word << "\n"; fileData.isSamplerState = 1;
	}
}

//******************************************************************************************************************
static GraphicsPipeline::BlendFactor toBlendFactor(string_view name, uint32 lineIndex)
{
	try { return toBlendFactor(name); }
	catch (const exception&)
	{ throw CompileError("unrecognized pipeline state blend factor type", lineIndex, string(name)); }
}
static GraphicsPipeline::BlendOperation toBlendOperation(string_view name, uint32 lineIndex)
{
	try { return toBlendOperation(name); }
	catch (const exception&)
	{ throw CompileError("unrecognized pipeline state blend operation type", lineIndex, string(name)); }
}

//******************************************************************************************************************
static void onShaderPipelineState(GraphicsFileData& fileData, GraphicsLineData& lineData,
	GraphicsPipeline::State& state, vector<GraphicsPipeline::BlendState>& blendStates)
{
	if (fileData.isPipelineState == 1)
	{
		if (lineData.word != "{")
			throw CompileError("undeclared pipeline state properties", fileData.lineIndex);
		fileData.outputFileStream << "// pipelineState\n// {";
		fileData.isPipelineState = 2;
	}
	else if (fileData.isPipelineState == 2)
	{
		if (lineData.word == "}")
		{
			fileData.outputFileStream << "// }"; fileData.isPipelineState = 0;
		}
		else
		{
			if (lineData.word == "topology") lineData.isTopology = 1;
			else if (lineData.word == "polygon") lineData.isPolygon = 1;
			else if (lineData.word == "discarding") lineData.isDiscarding = 1;
			else if (lineData.word == "depthTesting") lineData.isDepthTesting = 1;
			else if (lineData.word == "depthWriting") lineData.isDepthWriting = 1;
			else if (lineData.word == "depthClamping") lineData.isDepthClamping = 1;
			else if (lineData.word == "depthBiasing") lineData.isDepthBiasing = 1;
			else if (lineData.word == "depthCompare") lineData.isDepthCompare = 1;
			else if (lineData.word == "stencilTesting") lineData.isStencilTesting = 1;
			else if (lineData.word == "faceCulling") lineData.isFaceCulling = 1;
			else if (lineData.word == "cullFace") lineData.isCullFace = 1;
			else if (lineData.word == "frontFace") lineData.isFrontFace = 1;
			else if (lineData.word.length() >= 9 && memcmp(lineData.word.c_str(), "colorMask", 9) == 0)
			{
				if (lineData.word.length() > 9) // TODO: check strtoul for overflow
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 9, nullptr, 10);
				else
					throw CompileError("no colorMask blend state index", fileData.lineIndex);
				lineData.isColorMask = 1;
			}
			else if (lineData.word.length() >= 8 && memcmp(lineData.word.c_str(), "blending", 8) == 0)
			{
				if (lineData.word.length() > 8)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 8, nullptr, 10);
				else
					throw CompileError("no blending blend state index", fileData.lineIndex);
				lineData.isBlending = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "srcBlendFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no src blend factor blend state index", fileData.lineIndex);
				lineData.isSrcBlendFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "dstBlendFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no dst blend factor blend state index", fileData.lineIndex);
				lineData.isDstBlendFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "srcColorFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no src color factor blend state index", fileData.lineIndex);
				lineData.isSrcColorFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "dstColorFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no dst color factor blend state index", fileData.lineIndex);
				lineData.isDstColorFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "srcAlphaFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no src alpha factor blend state index", fileData.lineIndex);
				lineData.isSrcAlphaFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "dstAlphaFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no dst alpha factor blend state index", fileData.lineIndex);
				lineData.isDstAlphaFactor = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "blendOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no blend operation blend state index", fileData.lineIndex);
				lineData.isBlendOperation = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "colorOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no color operation blend state index", fileData.lineIndex);
				lineData.isColorOperation = 1;
			}
			else if (lineData.word.length() >= 14 && memcmp(lineData.word.c_str(), "alphaOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtoul(lineData.word.c_str() + 14, nullptr, 10);
				else
					throw CompileError("no alpha operation blend state index", fileData.lineIndex);
				lineData.isAlphaOperation = 1;
			}
			else throw CompileError("unrecognized pipeline state property", fileData.lineIndex, lineData.word);

			fileData.outputFileStream << "//     " << lineData.word << " ";
			fileData.isPipelineState = 3;
		}
	}
	else if (fileData.isPipelineState == 3)
	{
		if (lineData.word != "=")
			throw CompileError("no '=' after pipeline state property name", fileData.lineIndex);
		fileData.outputFileStream << "= "; fileData.isPipelineState = 4;
	}
	else
	{
		auto name = string_view(lineData.word.c_str(), lineData.word.find_first_of(';'));
		if (lineData.word.length() == name.length())
			throw CompileError("no ';' after pipeline state property name", fileData.lineIndex, string(name));

		if (lineData.isTopology)
		{
			try { state.topology = toTopology(name); }
			catch (const exception&)
			{ throw CompileError("unrecognized pipeline state topology type", fileData.lineIndex, string(name)); }
			lineData.isTopology = 0;
		}
		else if (lineData.isPolygon)
		{
			try { state.polygon = toPolygon(name); }
			catch (const exception&)
			{ throw CompileError("unrecognized pipeline state polygon type", fileData.lineIndex, string(name)); }
			lineData.isPolygon = 0;
		}
		else if (lineData.isDiscarding)
		{ state.discarding = toBoolState(name, fileData.lineIndex); lineData.isDiscarding = 0; }
		else if (lineData.isDepthTesting)
		{ state.depthTesting = toBoolState(name, fileData.lineIndex); lineData.isDepthTesting = 0; }
		else if (lineData.isDepthWriting)
		{ state.depthWriting = toBoolState(name, fileData.lineIndex); lineData.isDepthWriting = 0; }
		else if (lineData.isDepthClamping)
		{ state.depthClamping = toBoolState(name, fileData.lineIndex); lineData.isDepthClamping = 0; }
		else if (lineData.isDepthBiasing)
		{ state.depthBiasing = toBoolState(name, fileData.lineIndex); lineData.isDepthBiasing = 0; }
		else if (lineData.isDepthCompare)
		{ state.depthCompare = toCompareOperation(name, fileData.lineIndex); lineData.isDepthCompare = 0; }
		else if (lineData.isStencilTesting)
		{ state.stencilTesting = toBoolState(name, fileData.lineIndex); lineData.isStencilTesting = 0; }
		else if (lineData.isFaceCulling)
		{ state.faceCulling = toBoolState(name, fileData.lineIndex); lineData.isFaceCulling = 0; }
		else if (lineData.isCullFace)
		{
			try { state.cullFace = toCullFace(name); }
			catch (const exception&)
			{ throw CompileError("unrecognized pipeline state cull face type", fileData.lineIndex, string(name)); }
			lineData.isCullFace = 0;
		}
		else if (lineData.isFrontFace)
		{
			try { state.frontFace = toFrontFace(name); }
			catch (const exception&)
			{ throw CompileError("unrecognized pipeline state front face type", fileData.lineIndex, string(name)); }
			lineData.isFrontFace = 0;
		}
		else if (lineData.isColorMask)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].colorMask = toColorComponents(lineData.word);
			lineData.isColorMask = 0;
		}
		else if (lineData.isBlending)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].blending = toBoolState(name, fileData.lineIndex);
			lineData.isBlending = 0;
		}
		else if (lineData.isSrcBlendFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcColorFactor =
				blendStates[fileData.blendStateIndex].srcAlphaFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcBlendFactor = 0;
		}
		else if (lineData.isDstBlendFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstColorFactor =
				blendStates[fileData.blendStateIndex].dstAlphaFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isDstBlendFactor = 0;
		}
		else if (lineData.isSrcColorFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcColorFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcColorFactor = 0;
		}
		else if (lineData.isDstColorFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstColorFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isDstColorFactor = 0;
		}
		else if (lineData.isSrcAlphaFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcAlphaFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcAlphaFactor = 0;
		}
		else if (lineData.isDstAlphaFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstAlphaFactor = toBlendFactor(name, fileData.lineIndex);
			lineData.isDstAlphaFactor = 0;
		}
		else if (lineData.isBlendOperation)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].colorOperation =
				blendStates[fileData.blendStateIndex].alphaOperation =
				toBlendOperation(name, fileData.lineIndex);
			lineData.isBlendOperation = 0;
		}
		else if (lineData.isColorOperation)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].colorOperation = toBlendOperation(name, fileData.lineIndex);
			lineData.isColorOperation = 0;
		}
		else if (lineData.isAlphaOperation)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].alphaOperation = toBlendOperation(name, fileData.lineIndex);
			lineData.isAlphaOperation = 0;
		}
		else abort();

		fileData.outputFileStream << lineData.word; fileData.isPipelineState = 2;
	}
}

//******************************************************************************************************************
static void onSpecConst(FileData& fileData, LineData& lineData,
	Pipeline::SpecConsts& specConsts, ShaderStage shaderStage)
{
	if (lineData.isSpecConst == 1)
	{
		if (lineData.word != "const")
			throw CompileError("no specialization 'const' keyword", fileData.lineIndex, lineData.word);
		lineData.isSpecConst = 2;
		return;
	}
	else if (lineData.isSpecConst == 2)
	{
		try { lineData.dataType = toGslDataType(lineData.word); }
		catch (const exception&)
		{ throw CompileError("unrecognized spec const GSL data type", fileData.lineIndex, lineData.word); }

		fileData.outputFileStream << "layout(constant_id = " <<
			to_string(fileData.specConstIndex) << ") const " << lineData.word;
		lineData.isSpecConst = 3;
		return;
	}
	else if (lineData.isSpecConst == 3)
	{
		if (lineData.word.length() > UINT8_MAX)
			throw CompileError("too long spec const name", fileData.lineIndex, lineData.word);

		auto result = specConsts.find(lineData.word);
		if (result == specConsts.end())
		{
			Pipeline::SpecConst data;
			data.shaderStages = shaderStage;
			data.dataType = lineData.dataType;
			data.index = fileData.specConstIndex++;

			if (!specConsts.emplace(lineData.word, data).second)
				throw CompileError("different spec consts with the same name", fileData.lineIndex, lineData.word);
		}
		else
		{
			if (lineData.dataType != result->second.dataType)
				throw CompileError("different spec consts with the same name", fileData.lineIndex, lineData.word);
			result.value().shaderStages |= shaderStage;
		}

		fileData.outputFileStream << " " << lineData.word;
		lineData.isSpecConst = 4;
		return;
	}
	else if (lineData.isSpecConst == 4)
	{
		if (lineData.word != "=")
			throw CompileError("no '=' after spec const declaration", fileData.lineIndex);
		lineData.isSpecConst = 5;
		fileData.outputFileStream << " = ";
		return;
	}

	if (lineData.word.find_first_of(';') == string::npos)
		throw CompileError("no ';' after spec const value", fileData.lineIndex);
	fileData.outputFileStream << lineData.word;
	lineData.isSpecConst = 0;
}

//******************************************************************************************************************
static void onShaderFeature(FileData& fileData, LineData& lineData)
{
	if (lineData.word == "ext.debugPrintf")
		fileData.outputFileStream << "#extension GL_EXT_debug_printf : require";
	else if (lineData.word == "ext.explicitTypes")
		fileData.outputFileStream << "#extension GL_EXT_shader_explicit_arithmetic_types : require";
	else if (lineData.word == "ext.int8BitStorage")
		fileData.outputFileStream << "#extension GL_EXT_shader_8bit_storage : require";
	else if (lineData.word == "ext.int16BitStorage")
		fileData.outputFileStream << "#extension GL_EXT_shader_16bit_storage : require";
	else if (lineData.word == "ext.bindless")
		fileData.outputFileStream << "#extension GL_EXT_nonuniform_qualifier : require";
	else if (lineData.word == "ext.scalarLayout")
		fileData.outputFileStream << "#extension GL_EXT_scalar_block_layout : require";
	else if (lineData.word == "ext.bufferReference")
		fileData.outputFileStream << "#extension GL_EXT_buffer_reference : require";
	else if (lineData.word == "ext.subgroupBasic")
		fileData.outputFileStream << "#extension GL_KHR_shader_subgroup_basic : require";
	else if (lineData.word == "ext.subgroupVote")
		fileData.outputFileStream << "#extension GL_KHR_shader_subgroup_vote : require";
	else if (lineData.word == "ext.rayQuery")
		fileData.outputFileStream << "#extension GL_EXT_ray_query : require\n#include \"ray-query.gsl\"";
	else throw CompileError("unknown GSL feature", fileData.lineIndex, lineData.word);
	lineData.isFeature = false;
}

static void onShaderVariantCount(FileData& fileData, LineData& lineData, uint8& variantCount)
{
	auto count = strtoul(lineData.word.c_str(), nullptr, 10);
	if (count <= 1 || count > UINT8_MAX)
		throw CompileError("invalid variant count", fileData.lineIndex, lineData.word);
	variantCount = (uint8)count;
	fileData.outputFileStream << "layout(constant_id = 0) const uint gsl_variant = 0; ";
	lineData.isVariantCount = false;
}

//******************************************************************************************************************
static void replaceVaribaleDot(string& word, const char* compare, bool toUpper = false) noexcept
{
	auto dotOffset = strlen(compare) - 1;
	while (true)
	{
		auto index = word.find(compare);
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + dotOffset] = '_';
			if (toUpper)
				word[index + dotOffset + 1] = toupper(word[index + 3]);
			continue;
		}
		break;
	}
}
static void onShaderGlobalVariable(string& word) noexcept
{
	if (word.length() > 3)
	{
		replaceVaribaleDot(word, "vs.");
		replaceVaribaleDot(word, "fs.");
		replaceVaribaleDot(word, "fb.");
		replaceVaribaleDot(word, "gl.", true);
	}

	if (word.length() > 4)
		replaceVaribaleDot(word, "gsl.");
}

//******************************************************************************************************************
static bool openShaderFileStream(const fs::path& inputFilePath,
	const fs::path& outputFilePath, ifstream& inputFileStream, ofstream& outputFileStream)
{
	inputFileStream = ifstream(inputFilePath);
	if (!inputFileStream.is_open())
		return false;

	auto directory = outputFilePath.parent_path();
	if (!fs::exists(directory))
		fs::create_directories(directory);
	outputFileStream = ofstream(outputFilePath);
	if (!outputFileStream.is_open())
		throw CompileError("failed to open output shader file");
	outputFileStream.exceptions(ios::failbit | ios::badbit);

	outputFileStream << "#version 460\n#include \"types.gsl\"\n\n#define printf debugPrintfEXT\n"
		"#line 1 // Note: Compiler error is at the source shader file line!\n";
	return true;
}
static void compileShaderFile(const fs::path& filePath, const vector<fs::path>& includePaths)
{
	auto command = "glslc --target-env=" GARDEN_VULKAN_SHADER_VERSION_STRING 
		" -c -O \"" + filePath.generic_string() + "\" -o \"" + filePath.generic_string() + ".spv\"";
	for (auto& path : includePaths)
		command += " -I \"" + path.generic_string() + "\"";

	std::cout << std::flush;
	auto result = std::system(command.c_str());
	if (result != 0)
		throw GardenError("_GLSLC");

	// On some systems file can be still locked.
	auto attemptCount = 0;
	while (attemptCount < 10)
	{
		error_code errorCode;
		fs::remove(filePath, errorCode);
		if (errorCode)
			this_thread::sleep_for(chrono::milliseconds(1));
		else break;
	}
}

//******************************************************************************************************************
template<typename T>
static void writeGslHeaderValues(const fs::path& filePath,
	string_view gslMagic, ofstream& headerStream, const T& values)
{
	headerStream.open(filePath, ios::out | ios::binary);
	if (!headerStream.is_open())
		throw CompileError("failed to open header file");
	headerStream.write((const char*)gslMagic.data(), gslMagic.length());
	headerStream.write((const char*)gslHeader, sizeof(gslHeader));
	headerStream.write((const char*)&values, sizeof(T));
}

template<typename T, typename A>
static void writeGslHeaderArray(ofstream& headerStream, const A& valueArray)
{
	for (const auto& pair : valueArray)
	{
		GARDEN_ASSERT(pair.first.length() <= UINT8_MAX);
		auto length = (uint8)pair.first.length();
		headerStream.write((char*)&length, sizeof(uint8));
		headerStream.write(pair.first.c_str(), length);
		headerStream.write((const char*)&pair.second, sizeof(T));
	}
}

//******************************************************************************************************************
static bool processCommonKeywords(Pipeline::CreateData& data, FileData& fileData, 
	LineData& lineData, bool& overrideOutput, uint8& bindingIndex, 
	uint16& pushConstantsSize, uint8& variantCount, ShaderStage shaderStage)
{
	if (fileData.isUniform)
	{
		onShaderUniform(fileData, lineData, shaderStage, bindingIndex, data.uniforms, data.samplerStates);
		overrideOutput = true; return true;
	}
	if (fileData.isPushConstants)
	{
		onShaderPushConstants(fileData, lineData, pushConstantsSize);
		return true;
	}
	if (fileData.isSamplerState)
	{
		onShaderSamplerState(fileData, lineData);
		overrideOutput = true; return true;
	}
	if (lineData.isSpecConst)
	{
		onSpecConst(fileData, lineData, data.specConsts, shaderStage);
		overrideOutput = true; return true;
	}
	if (lineData.isFeature)
	{
		onShaderFeature(fileData, lineData);
		overrideOutput = true; return true;
	}
	if (lineData.isVariantCount)
	{
		onShaderVariantCount(fileData, lineData, variantCount);
		overrideOutput = true; return true;
	}
	return false;
}
static bool setCommonKeywords(FileData& fileData, LineData& lineData, bool& overrideOutput)
{
	overrideOutput = true;
	if (lineData.word == "uniform") { fileData.isUniform = 1; return true; }
	if (lineData.word == "buffer") { fileData.isUniform = fileData.isBuffer = 1; return true; }
	if (lineData.word == "spec") { lineData.isSpecConst = 1; return true; }
	if (lineData.word == "#feature") { lineData.isFeature = true; return true; }
	if (lineData.word == "#variantCount") { lineData.isVariantCount = true; return true; }
	if (lineData.word == "pushConstants") throw CompileError("missing 'uniform' keyword", fileData.lineIndex);
	overrideOutput = false;
	return false;
}

//******************************************************************************************************************
static bool compileGraphicsShader(const fs::path& inputPath, const fs::path& outputPath,
	const vector<fs::path>& includePaths, GslCompiler::GraphicsData& data, uint8& bindingIndex, 
	uint8& inIndex, uint8& outIndex, uint16& pushConstantsSize, uint8& variantCount, ShaderStage shaderStage)
{
	GraphicsFileData fileData;
	auto filePath = data.shaderPath;
	if (shaderStage == ShaderStage::Vertex) filePath += ".vert";
	else if (shaderStage == ShaderStage::Fragment) filePath += ".frag";
	else abort();

	auto inputFilePath = inputPath / filePath, outputFilePath = outputPath / filePath;
	auto fileResult = openShaderFileStream(inputFilePath, outputFilePath,
		fileData.inputFileStream, fileData.outputFileStream);
	if (!fileResult) return false;
	
	while (getline(fileData.inputFileStream, fileData.line))
	{
		fileData.lineIndex++;
		if (fileData.line.empty())
		{
			fileData.outputFileStream << "\n";
			continue;
		}

		fileData.inputStream.str(fileData.line);
		fileData.inputStream.clear();

		GraphicsLineData lineData;
		auto outPosition = fileData.outputFileStream.tellp();
		uint32 wordIndex = 0;

		while (fileData.inputStream >> lineData.word)
		{
			wordIndex++;
			if (lineData.word.empty()) continue;

			if (lineData.word.length() >= 2)
			{
				if (lineData.word[0] == '/' && lineData.word[1] == '/')
				{
					do fileData.outputFileStream << lineData.word << " ";
					while (fileData.inputStream >> lineData.word);
					break;
				}
				else if (lineData.word[0] == '/' && lineData.word[1] == '*') fileData.isSkipMode = true;
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos) fileData.isSkipMode = false;
				continue;
				// TODO: offset word and process it.
				// Also incorrect line count if uniform sampler2D sampler; // Has some comment
			}
			
			auto overrideOutput = false;
			if (lineData.isIn)
			{
				if (shaderStage == ShaderStage::Vertex)
				{
					if (lineData.isIn == 1)
					{
						try { lineData.dataType = toGslDataType(lineData.word); }
						catch (const exception&)
						{
							throw CompileError("unrecognized vertex attribute "
								"GSL data type", fileData.lineIndex, lineData.word);
						}

						fileData.outputFileStream << "layout(location = " << to_string(fileData.inIndex) << ") in ";
						fileData.inIndex += toLocationOffset(lineData.dataType); lineData.isIn = 2;
					}
					else if (lineData.isIn == 2)
					{
						onShaderGlobalVariable(lineData.word);
						fileData.outputFileStream << lineData.word;
						overrideOutput = true; lineData.isIn = 3;
					}
					else if (lineData.isIn == 3)
					{
						if (lineData.word == ":") { overrideOutput = true; lineData.isIn = 4; }
						else throw CompileError("no ':' after vertex attribute name", fileData.lineIndex);
					}
					else
					{
						auto name = string(lineData.word, 0, lineData.word.find_first_of(';'));
						GslDataFormat format;
						try { format = toGslDataFormat(name); }
						catch (const exception&)
						{
							throw CompileError("unrecognized vertex attribute "
								"GSL data format", fileData.lineIndex, lineData.word);
						}

						GraphicsPipeline::VertexAttribute vertexAttribute;
						vertexAttribute.type = lineData.dataType;
						vertexAttribute.format = format;
						vertexAttribute.offset = data.vertexAttributesSize;
						data.vertexAttributes.push_back(vertexAttribute); 
						data.vertexAttributesSize += (uint16)(
							toComponentCount(vertexAttribute.type) * toBinarySize(vertexAttribute.format));
						fileData.outputFileStream << "; // " << toString(format);
						overrideOutput = true; lineData.isIn = 0;
					}
				}
				else
				{
					if (lineData.word == "flat") { lineData.isFlat = true; overrideOutput = true; }
					else if (lineData.word == "noperspective") { lineData.isNoperspective = true; overrideOutput = true; }
					else
					{
						try { lineData.dataType = toGslDataType(lineData.word); }
						catch (const exception&)
						{ throw CompileError("unrecognized in GSL data type", fileData.lineIndex, lineData.word); }

						fileData.outputFileStream << "layout(location = " << to_string(inIndex) << ") in ";
						if (lineData.isFlat) fileData.outputFileStream << "flat ";
						if (lineData.isNoperspective) fileData.outputFileStream << "noperspective ";
						inIndex += toLocationOffset(lineData.dataType); lineData.isIn = 0;
					}
				}
			}
			else if (lineData.isOut)
			{
				if (shaderStage == ShaderStage::Vertex)
				{
					if (lineData.word == "flat") { lineData.isFlat = true; overrideOutput = true; }
					else if (lineData.word == "noperspective") { lineData.isNoperspective = true; overrideOutput = true; }
					else
					{
						try { lineData.dataType = toGslDataType(lineData.word); }
						catch (const exception&)
						{ throw CompileError("unrecognized out GSL data type", fileData.lineIndex, lineData.word); }

						fileData.outputFileStream << "layout(location = " << to_string(outIndex) << ") out ";
						if (lineData.isFlat) fileData.outputFileStream << "flat ";
						if (lineData.isNoperspective) fileData.outputFileStream << "noperspective ";
						outIndex += toLocationOffset(lineData.dataType); lineData.isOut = 0;
					}
				}
				else
				{
					if (lineData.isOut == 1)
					{
						try { lineData.dataType = toGslDataType(lineData.word); }
						catch (const exception&)
						{ throw CompileError("unrecognized out GSL data type", fileData.lineIndex, lineData.word); }

						fileData.outputFileStream << "layout(location = " << to_string(fileData.outIndex) << ") out ";
						fileData.outIndex += toLocationOffset(lineData.dataType); lineData.isOut = 2; 
					}
					else if (lineData.isOut == 2) { onShaderGlobalVariable(lineData.word); lineData.isOut = 0; }
				}
			}
			else if (fileData.isPipelineState)
			{
				onShaderPipelineState(fileData, lineData, data.pipelineState, data.blendStates);
				overrideOutput = true;
			}
			else if (lineData.isAttachmentOffset)
			{
				auto offset = strtoul(lineData.word.c_str(), nullptr, 10);
				if (offset > UINT8_MAX)
					throw CompileError("invalid subpass input offset", fileData.lineIndex, lineData.word);
				fileData.outputFileStream << "// #attachmentOffset ";
				fileData.attachmentIndex += (uint8)offset; lineData.isAttachmentOffset = 0;
			}
			else if (lineData.isAttributeOffset)
			{
				auto offset = strtoul(lineData.word.c_str(), nullptr, 10);
				if (offset > UINT16_MAX)
					throw CompileError("invalid vertex attribute binary offset", fileData.lineIndex, lineData.word);
				fileData.outputFileStream << "// #attributeOffset ";
				data.vertexAttributesSize += (uint16)offset; lineData.isAttributeOffset = 0;
			}
			else if (processCommonKeywords(data, fileData, lineData, overrideOutput, 
				bindingIndex, pushConstantsSize, variantCount, shaderStage)) { }
			else
			{
				if (lineData.word == "in" && wordIndex == 1) { lineData.isIn = 1; overrideOutput = true; }
				else if (lineData.word == "out" && wordIndex == 1 && !lineData.isDepthOverride)
				{ lineData.isOut = 1; overrideOutput = true; }
				else if (lineData.word == "pipelineState") { fileData.isPipelineState = 1; overrideOutput = true; }
				else if (lineData.word == "#attachmentOffset") { lineData.isAttachmentOffset = 1; overrideOutput = true; }
				else if (lineData.word == "#attributeOffset")
				{
					if (shaderStage != ShaderStage::Vertex)
						throw CompileError("#attributeOffset is accessible only in vertex shaders", fileData.lineIndex);
					lineData.isAttributeOffset = 1; overrideOutput = true;
				}
				else if (lineData.word == "depthLess")
				{
					if (shaderStage != ShaderStage::Fragment)
						throw CompileError("depthLess is accessible only in fragment shaders", fileData.lineIndex);
					fileData.outputFileStream << "layout(depth_less) ";
					lineData.isDepthOverride = 1; overrideOutput = true;
				}
				else if (lineData.word == "depthGreater")
				{
					if (shaderStage != ShaderStage::Fragment)
						throw CompileError("depthGreater is accessible only in fragment shaders", fileData.lineIndex);
					fileData.outputFileStream << "layout(depth_greater) ";
					lineData.isDepthOverride = 1; overrideOutput = true;
				}
				else if (lineData.word == "earlyFragmentTests")
				{
					if (shaderStage != ShaderStage::Fragment)
						throw CompileError("earlyFragmentTests is accessible only in fragment shaders", fileData.lineIndex);
					fileData.outputFileStream << "layout(early_fragment_tests) ";
					overrideOutput = true;
				}
				else if (setCommonKeywords(fileData, lineData, overrideOutput)) { }
				else onShaderGlobalVariable(lineData.word);
			}

			if (!overrideOutput) fileData.outputFileStream << lineData.word << " ";
			lineData.isNewLine = false;
		}

		if (outPosition != fileData.outputFileStream.tellp())
			fileData.outputFileStream << "\n";
	}

	if (shaderStage == ShaderStage::Fragment)
	{
		if (data.blendStates.size() < fileData.outIndex)
			data.blendStates.resize(fileData.outIndex);
	}

	fileData.outputFileStream.close();
	compileShaderFile(outputFilePath, includePaths);

	vector<uint8>* shaderCode;
	if (shaderStage == ShaderStage::Vertex) shaderCode = &data.vertexCode;
	else if (shaderStage == ShaderStage::Fragment) shaderCode = &data.fragmentCode;
	else abort();

	outputFilePath += ".spv";
	if (!File::tryLoadBinary(outputFilePath, *shaderCode))
		throw CompileError("failed to open compiled shader file");
	if (shaderCode->empty())
		throw CompileError("compiled shader file is empty");
	return true;
}

//******************************************************************************************************************
bool GslCompiler::compileGraphicsShaders(const fs::path& inputPath,
	const fs::path& outputPath, const vector<fs::path>& includePaths, GraphicsData& data)
{
	GARDEN_ASSERT(!data.shaderPath.empty());
	uint16 vertexPushConstantsSize = 0, fragmentPushConstantsSize = 0;
	uint8 vertexVariantCount = 1, fragmentVariantCount = 1;
	uint8 bindingIndex = 0, outIndex = 0, inIndex = 0;

	// TODO: support mesh and task shaders.

	auto compileResult = compileGraphicsShader(inputPath, outputPath, includePaths, data, bindingIndex, 
		inIndex, outIndex, vertexPushConstantsSize, vertexVariantCount, ShaderStage::Vertex);
	compileResult |= compileGraphicsShader(inputPath, outputPath, includePaths, data, bindingIndex, 
		inIndex, outIndex, fragmentPushConstantsSize, fragmentVariantCount, ShaderStage::Fragment);
	if (!compileResult) return false;

	if (!data.fragmentCode.empty() && !data.vertexCode.empty() && outIndex != inIndex)
		throw CompileError("different vertex and fragment shader in/out count");
	if (vertexVariantCount > 1 && fragmentVariantCount > 1 && vertexVariantCount != fragmentVariantCount)
		throw CompileError("different vertex and fragment shader variant count");
	if (vertexPushConstantsSize > 0 && fragmentPushConstantsSize > 0 && 
		vertexPushConstantsSize != fragmentPushConstantsSize)
	{
		throw CompileError("different vertex and fragment shader push constants size");
	}

	if (outIndex > 0 && data.fragmentCode.empty())
		throw CompileError("vertex shader has output but no fragment shader exist");
	if (inIndex > 0 && data.vertexCode.empty())
		throw CompileError("fragment shader has input but no vertex shader exist");

	GARDEN_ASSERT(data.vertexAttributes.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.blendStates.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.uniforms.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.samplerStates.size() <= UINT8_MAX);

	data.pushConstantsStages = ShaderStage::None;
	if (vertexPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Vertex;
	if (fragmentPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Fragment;

	if (vertexPushConstantsSize > 0) data.pushConstantsSize = vertexPushConstantsSize;
	else if (fragmentPushConstantsSize > 0) data.pushConstantsSize = fragmentPushConstantsSize;

	if (vertexVariantCount > 1) data.variantCount = vertexVariantCount;
	else if (fragmentVariantCount > 1) data.variantCount = fragmentVariantCount;
	else data.variantCount = 1;

	data.descriptorSetCount = 0;
	for (const auto& uniform : data.uniforms)
	{
		if (uniform.second.descriptorSetIndex > data.descriptorSetCount)
			data.descriptorSetCount = uniform.second.descriptorSetIndex;
	}
	data.descriptorSetCount++;

	GraphicsGslValues values;
	values.uniformCount = (uint8)data.uniforms.size();
	values.samplerStateCount = (uint8)data.samplerStates.size();
	values.descriptorSetCount = data.descriptorSetCount;
	values.variantCount = data.variantCount;
	values.pushConstantsSize = data.pushConstantsSize;
	values.specConstCount = (uint8)data.specConsts.size();
	values.vertexAttributeCount = (uint8)data.vertexAttributes.size();
	values.blendStateCount = (uint8)data.blendStates.size();
	values.vertexAttributesSize = data.vertexAttributesSize;
	values.pushConstantsStages = data.pushConstantsStages;
	values.pipelineState = data.pipelineState;
	
	ofstream headerStream;
	auto headerFilePath = outputPath / data.shaderPath; headerFilePath += ".gslh";
	writeGslHeaderValues(headerFilePath, graphicsGslMagic, headerStream, values);

	if (values.vertexAttributeCount > 0)
	{
		headerStream.write((const char*)data.vertexAttributes.data(),
			values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute));
	}
	if (values.blendStateCount > 0)
	{
		headerStream.write((const char*)data.blendStates.data(),
			values.blendStateCount * sizeof(GraphicsPipeline::BlendState));
	}
	
	writeGslHeaderArray<Pipeline::Uniform>(headerStream, data.uniforms);
	writeGslHeaderArray<Sampler::State>(headerStream, data.samplerStates);
	writeGslHeaderArray<Pipeline::SpecConst>(headerStream, data.specConsts);
	return true;
}

//******************************************************************************************************************
bool GslCompiler::compileComputeShader(const fs::path& inputPath,
	const fs::path& outputPath, const vector<fs::path>& includePaths, ComputeData& data)
{
	GARDEN_ASSERT(!data.shaderPath.empty());
	constexpr auto shaderStage = ShaderStage::Compute;

	ComputeFileData fileData;
	auto filePath = data.shaderPath; filePath += ".comp";
	auto inputFilePath = inputPath / filePath, outputFilePath = outputPath / filePath;
	auto fileResult = openShaderFileStream(inputFilePath, outputFilePath,
		fileData.inputFileStream, fileData.outputFileStream);
	if (!fileResult) return false;
	
	while (getline(fileData.inputFileStream, fileData.line))
	{
		fileData.lineIndex++;
		if (fileData.line.empty())
		{
			fileData.outputFileStream << "\n";
			continue;
		}
		
		fileData.inputStream.str(fileData.line);
		fileData.inputStream.clear();
		
		ComputeLineData lineData;
		auto outPosition = fileData.outputFileStream.tellp();
		
		while (fileData.inputStream >> lineData.word)
		{
			if (lineData.word.empty()) continue;

			if (lineData.word.length() >= 2)
			{
				if (lineData.word[0] == '/' && lineData.word[1] == '/')
				{
					do fileData.outputFileStream << lineData.word << " ";
					while (fileData.inputStream >> lineData.word);
					break;
				}
				else if (lineData.word[0] == '/' && lineData.word[1] == '*') fileData.isSkipMode = true;
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos) fileData.isSkipMode = false;
				continue;
			}

			auto overrideOutput = false;
			if (lineData.isLocalSize)
			{
				if (lineData.isLocalSize == 1)
				{
					if (lineData.word != "=")
						throw CompileError("no '=' after localSize declaration", fileData.lineIndex);
					lineData.isLocalSize = 2;
				}
				else if (lineData.isLocalSize == 4)
				{
					if (lineData.word.find_first_of(';') == string::npos)
						throw CompileError("no ';' after local size", fileData.lineIndex);
					data.localSize.z = strtoul(lineData.word.c_str(), nullptr, 10);
					if (data.localSize.z <= 0)
						throw CompileError("local size 'z' can not be less than one", fileData.lineIndex);
					fileData.outputFileStream << "layout(local_size_x = " <<
						data.localSize.x << ", local_size_y = " << data.localSize.y <<
						", local_size_z = " << data.localSize.z << ") in; ";
					lineData.isLocalSize = 0;
				}
				else
				{
					if (lineData.word.find_first_of(',') == string::npos)
						throw CompileError("no ',' after local size dimension", fileData.lineIndex);

					if (lineData.isLocalSize == 2)
					{
						data.localSize.x = strtoul(lineData.word.c_str(), nullptr, 10);
						if (data.localSize.x <= 0)
							throw CompileError("local size 'x' can not be less than one", fileData.lineIndex);
						lineData.isLocalSize = 3;
					}
					else if (lineData.isLocalSize == 3)
					{
						data.localSize.y = strtoul(lineData.word.c_str(), nullptr, 10);
						if (data.localSize.y <= 0)
							throw CompileError("local size 'y' can not be less than one", fileData.lineIndex);
						lineData.isLocalSize = 4;
					}
				}
				overrideOutput = true;
			}
			else if (processCommonKeywords(data, fileData, lineData, overrideOutput, 
				fileData.bindingIndex, data.pushConstantsSize, data.variantCount, shaderStage)) { }
			else
			{
				if (lineData.word == "localSize") { lineData.isLocalSize = 1; overrideOutput = true; }
				else if (setCommonKeywords(fileData, lineData, overrideOutput)) { }
				else onShaderGlobalVariable(lineData.word);
			}

			if (!overrideOutput) fileData.outputFileStream << lineData.word << " ";
			lineData.isNewLine = false;
		}

		if (outPosition != fileData.outputFileStream.tellp())
			fileData.outputFileStream << "\n";
	}

	if (data.localSize == uint3::zero)
		throw CompileError("undeclared work group localSize");

	GARDEN_ASSERT(data.uniforms.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.samplerStates.size() <= UINT8_MAX);
	
	fileData.outputFileStream.close();
	compileShaderFile(outputFilePath, includePaths);

	if (data.pushConstantsSize > 0) data.pushConstantsStages = shaderStage;
	if (data.variantCount == 0) data.variantCount = 1;

	data.descriptorSetCount = 0;
	for (const auto& uniform : data.uniforms)
	{
		if (uniform.second.descriptorSetIndex > data.descriptorSetCount)
			data.descriptorSetCount = uniform.second.descriptorSetIndex;
	}
	data.descriptorSetCount++;

	ComputeGslValues values;
	values.uniformCount = (uint8)data.uniforms.size();
	values.samplerStateCount = (uint8)data.samplerStates.size();
	values.descriptorSetCount = data.descriptorSetCount;
	values.variantCount = data.variantCount;
	values.pushConstantsSize = data.pushConstantsSize;
	values.specConstCount = (uint8)data.specConsts.size();
	values.localSize = data.localSize;
	
	ofstream headerStream;
	auto headerFilePath = outputPath / data.shaderPath; headerFilePath += ".gslh";
	writeGslHeaderValues(headerFilePath, computeGslMagic, headerStream, values);
	writeGslHeaderArray<Pipeline::Uniform>(headerStream, data.uniforms);
	writeGslHeaderArray<Sampler::State>(headerStream, data.samplerStates);
	writeGslHeaderArray<Pipeline::SpecConst>(headerStream, data.specConsts);
	headerStream.close();

	outputFilePath += ".spv";
	if (!File::tryLoadBinary(outputFilePath, data.code))
		throw CompileError("failed to open compiled shader file");
	if (data.code.empty())
		throw CompileError("compiled shader file is empty");
	return true;
}

//******************************************************************************************************************
static bool compileRayTracingShader(const fs::path& inputPath, const fs::path& outputPath,
	const vector<fs::path>& includePaths, GslCompiler::RayTracingData& data, 
	uint8& bindingIndex, uint16& pushConstantsSize, uint8& variantCount, 
	uint32& rayRecursionDepth, uint8 groupIndex, ShaderStage shaderStage)
{
	RayTracingFileData fileData;
	auto filePath = data.shaderPath; filePath += toShaderStageExt(shaderStage);
	auto inputFilePath = inputPath / filePath, outputFilePath = outputPath / filePath;
	auto fileResult = openShaderFileStream(inputFilePath, outputFilePath,
		fileData.inputFileStream, fileData.outputFileStream);
	if (!fileResult) return false;

	fileData.outputFileStream << "#extension GL_EXT_ray_tracing : require\n#include \"ray-tracing.gsl\"\n"
		"#line 1 // Note: Compiler error is at the source shader file line!\n";

	// TODO: shaderRecordEXT support
	
	while (getline(fileData.inputFileStream, fileData.line))
	{
		fileData.lineIndex++;
		if (fileData.line.empty())
		{
			fileData.outputFileStream << "\n";
			continue;
		}
		
		fileData.inputStream.str(fileData.line);
		fileData.inputStream.clear();
		
		RayTracingLineData lineData;
		auto outPosition = fileData.outputFileStream.tellp();
		
		while (fileData.inputStream >> lineData.word)
		{
			if (lineData.word.empty()) continue;

			if (lineData.word.length() >= 2)
			{
				if (lineData.word[0] == '/' && lineData.word[1] == '/')
				{
					do fileData.outputFileStream << lineData.word << " ";
					while (fileData.inputStream >> lineData.word);
					break;
				}
				else if (lineData.word[0] == '/' && lineData.word[1] == '*') fileData.isSkipMode = true;
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos) fileData.isSkipMode = false;
				continue;
			}

			auto overrideOutput = false;
			if (lineData.isRayPayload)
			{
				fileData.outputFileStream << "layout(location = " << 
					to_string(fileData.rayPayloadIndex++) << ") rayPayloadEXT ";
				lineData.isRayPayload = 0;
			}
			else if (lineData.isRayPayloadIn)
			{
				fileData.outputFileStream << "layout(location = " << 
					to_string(fileData.rayPayloadIndex++) << ") rayPayloadInEXT ";
				lineData.isRayPayloadIn = 0;
			}
			else if (lineData.isCallableData)
			{
				fileData.outputFileStream << "layout(location = " << 
					to_string(fileData.callableDataIndex++) << ") callableDataEXT ";
				lineData.isCallableData = 0;
			}
			else if (lineData.isCallableDataIn)
			{
				fileData.outputFileStream << "layout(location = " << 
					to_string(fileData.callableDataIndex++) << ") callableDataInEXT ";
				lineData.isCallableDataIn = 0;
			}
			else if (lineData.isRayPayloadOffset)
			{
				auto offset = strtoul(lineData.word.c_str(), nullptr, 10);
				if (offset > UINT8_MAX)
					throw CompileError("invalid ray payload index offset", fileData.lineIndex, lineData.word);
				fileData.outputFileStream << "// #rayPayloadOffset ";
				fileData.rayPayloadIndex += (uint8)offset; lineData.isRayPayloadOffset = 0;
			}
			else if (lineData.isCallableDataOffset)
			{
				auto offset = strtoul(lineData.word.c_str(), nullptr, 10);
				if (offset > UINT8_MAX)
					throw CompileError("invalid callable data index offset", fileData.lineIndex, lineData.word);
				fileData.outputFileStream << "// #callableDataOffset ";
				fileData.callableDataIndex += (uint8)offset; lineData.isCallableDataOffset = 0;
			}
			else if (lineData.isRayRecursionDepth)
			{
				auto depth = strtoul(lineData.word.c_str(), nullptr, 10);
				if (depth < 1)
					throw CompileError("invalid max ray recursion depth", fileData.lineIndex, lineData.word);
				fileData.outputFileStream << "#define gsl_rayRecursionDepth ";
				rayRecursionDepth = (uint32)depth; lineData.isRayRecursionDepth = 0;
			}
			else if (processCommonKeywords(data, fileData, lineData, overrideOutput, 
				bindingIndex, pushConstantsSize, variantCount, shaderStage)) { }
			else
			{
				if (lineData.word == "rayPayload")
				{
					if (shaderStage != ShaderStage::RayGeneration && shaderStage != 
						ShaderStage::ClosestHit && shaderStage != ShaderStage::Miss)
					{
						throw CompileError("rayPayload is accessible only in ray gen/chit/miss shaders", fileData.lineIndex);
					}
					lineData.isRayPayload = 1; overrideOutput = true;
				}
				else if (lineData.word == "rayPayloadIn")
				{
					if (shaderStage != ShaderStage::AnyHit && shaderStage != 
						ShaderStage::ClosestHit && shaderStage != ShaderStage::Miss)
					{
						throw CompileError("rayPayloadIn is accessible only in ray hit/miss shaders", fileData.lineIndex);
					}
					lineData.isRayPayloadIn = 1; overrideOutput = true;
				}
				else if (lineData.word == "callableData") { lineData.isCallableData = 1; overrideOutput = true; }
				else if (lineData.word == "callableDataIn") { lineData.isCallableDataIn = 1; overrideOutput = true; }
				else if (lineData.word == "#rayPayloadOffset") { lineData.isRayPayloadOffset = 1; overrideOutput = true; }
				else if (lineData.word == "#callableDataOffset") { lineData.isCallableDataOffset = 1; overrideOutput = true; }
				else if (lineData.word == "#rayRecursionDepth") { lineData.isRayRecursionDepth = 1; overrideOutput = true; }
				else if (setCommonKeywords(fileData, lineData, overrideOutput)) { }
				else onShaderGlobalVariable(lineData.word);
			}

			if (!overrideOutput) fileData.outputFileStream << lineData.word << " ";
			lineData.isNewLine = false;
		}

		if (outPosition != fileData.outputFileStream.tellp())
			fileData.outputFileStream << "\n";
	}
	
	fileData.outputFileStream.close();
	compileShaderFile(outputFilePath, includePaths);

	vector<uint8>* shaderCode;
	if (shaderStage == ShaderStage::RayGeneration)
	{
		if (data.rayGenGroups.size() <= groupIndex) data.rayGenGroups.push_back({});
		shaderCode = &data.rayGenGroups[groupIndex];
	}
	else if (shaderStage == ShaderStage::Miss)
	{
		if (data.missGroups.size() <= groupIndex) data.missGroups.push_back({});
		shaderCode = &data.missGroups[groupIndex];
	}
	else if (shaderStage == ShaderStage::Callable)
	{
		if (data.callGroups.size() <= groupIndex) data.callGroups.push_back({});
		shaderCode = &data.callGroups[groupIndex];
	}
	else if (shaderStage == ShaderStage::Intersection)
	{
		if (data.hitGroups.size() <= groupIndex) data.hitGroups.push_back({});
		shaderCode = &data.hitGroups[groupIndex].intersectionCode;
	}
	else if (shaderStage == ShaderStage::AnyHit)
	{
		if (data.hitGroups.size() <= groupIndex) data.hitGroups.push_back({});
		shaderCode = &data.hitGroups[groupIndex].anyHitCode;
	}
	else if (shaderStage == ShaderStage::ClosestHit)
	{
		if (data.hitGroups.size() <= groupIndex) data.hitGroups.push_back({});
		shaderCode = &data.hitGroups[groupIndex].closestHitCode;
	}
	else abort();

	outputFilePath += ".spv";
	if (!File::tryLoadBinary(outputFilePath, *shaderCode))
		throw CompileError("failed to open compiled shader file");
	if (shaderCode->empty())
		throw CompileError("compiled shader file is empty");
	return true;
}

//******************************************************************************************************************
namespace garden
{
	struct RayTracingShaderValues final
	{
		uint16 rayGenPushConstantsSize = 0, missPushConstantsSize = 0, callPushConstantsSize = 0,
			intersectPushConstantsSize = 0, anyHitPushConstantsSize = 0, closHitPushConstantsSize = 0;
		uint8 rayGenVariantCount = 1, missVariantCount = 1, callVariantCount = 1, 
			intersectVariantCount = 1, anyHitVariantCount = 1, closHitVariantCount = 1;
		uint32 rayGenRayRecDepth = 1, missRayRecDepth = 1, callRayRecDepth = 1,
			intersectRayRecDepth = 1, anyHitRayRecDepth = 1, closHitRayRecDepth = 1;
	};
}
static void checkRtShaderValues(const RayTracingShaderValues& b, const RayTracingShaderValues& v)
{
	if (b.rayGenVariantCount > 1 && v.rayGenVariantCount > 1 && b.rayGenVariantCount != v.rayGenVariantCount)
		throw CompileError("different ray generation shaders variant count");
	if (b.rayGenVariantCount > 1 && v.missVariantCount > 1 && b.rayGenVariantCount != v.missVariantCount)
		throw CompileError("different ray generation and miss shader variant count");
	if (b.rayGenVariantCount > 1 && v.callVariantCount > 1 && b.rayGenVariantCount != v.callVariantCount)
		throw CompileError("different ray generation and callable shader variant count");
	if (b.rayGenVariantCount > 1 && v.intersectVariantCount > 1 && b.rayGenVariantCount != v.intersectVariantCount)
		throw CompileError("different ray generation and intersection shader variant count");
	if (b.rayGenVariantCount > 1 && v.anyHitVariantCount > 1 && b.rayGenVariantCount != v.anyHitVariantCount)
		throw CompileError("different ray generation and any hit shader variant count");
	if (b.rayGenVariantCount > 1 && v.closHitVariantCount > 1 && b.rayGenVariantCount != v.closHitVariantCount)
		throw CompileError("different ray generation and closest hit shader variant count");

	if (b.rayGenRayRecDepth > 1 && v.rayGenRayRecDepth > 1 && b.rayGenRayRecDepth != v.rayGenRayRecDepth)
		throw CompileError("different ray generation shaders ray recursion depth");
	if (b.rayGenRayRecDepth > 1 && v.missRayRecDepth > 1 && b.rayGenRayRecDepth != v.missRayRecDepth)
		throw CompileError("different ray generation and miss shader ray recursion depth");
	if (b.rayGenRayRecDepth > 1 && v.callRayRecDepth > 1 && b.rayGenRayRecDepth != v.callRayRecDepth)
		throw CompileError("different ray generation and callable shader ray recursion depth");
	if (b.rayGenRayRecDepth > 1 && v.intersectRayRecDepth > 1 && b.rayGenRayRecDepth != v.intersectRayRecDepth)
		throw CompileError("different ray generation and intersection shader ray recursion depth");
	if (b.rayGenRayRecDepth > 1 && v.anyHitRayRecDepth > 1 && b.rayGenRayRecDepth != v.anyHitRayRecDepth)
		throw CompileError("different ray generation and any hit shader ray recursion depth");
	if (b.rayGenRayRecDepth > 1 && v.closHitRayRecDepth > 1 && b.rayGenRayRecDepth != v.closHitRayRecDepth)
		throw CompileError("different ray generation and closest hit shader ray recursion depth");
}

//******************************************************************************************************************
bool GslCompiler::compileRayTracingShaders(const fs::path& inputPath, 
	const fs::path& outputPath, const vector<fs::path>& includePaths, RayTracingData& data)
{
	GARDEN_ASSERT(!data.shaderPath.empty());
	RayTracingShaderValues bValues; uint8 bindingIndex = 0;
	uint8 rayGenGroupIndex = 0, missGroupIndex = 0, callGroupIndex = 0, hitGroupIndex = 0;

	auto compileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.rayGenPushConstantsSize, bValues.rayGenVariantCount, 
		bValues.rayGenRayRecDepth, rayGenGroupIndex, ShaderStage::RayGeneration);
	compileResult &= compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.missPushConstantsSize, bValues.missVariantCount, 
		bValues.missRayRecDepth, missGroupIndex, ShaderStage::Miss);
	auto hitCompileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.anyHitPushConstantsSize, bValues.anyHitVariantCount, 
		bValues.anyHitRayRecDepth, hitGroupIndex, ShaderStage::AnyHit);
	hitCompileResult |= compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.closHitPushConstantsSize, bValues.closHitVariantCount, 
		bValues.closHitRayRecDepth, hitGroupIndex, ShaderStage::ClosestHit);
	hitCompileResult |= compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.intersectPushConstantsSize, bValues.intersectVariantCount, 
		bValues.intersectRayRecDepth, hitGroupIndex, ShaderStage::Intersection);
	if (!compileResult || !hitCompileResult) return false;

	auto callCompileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
		data, bindingIndex, bValues.callPushConstantsSize, bValues.callVariantCount, 
		bValues.callRayRecDepth, callGroupIndex, ShaderStage::Callable);
	if (callCompileResult) callGroupIndex++;

	checkRtShaderValues(bValues, bValues);
	rayGenGroupIndex++; missGroupIndex++; hitGroupIndex++;
	data.pushConstantsStages = ShaderStage::None;

	uint8 groupIndex = 1;
	while (true)
	{
		RayTracingShaderValues hgValues;
		data.shaderPath.replace_extension(to_string(groupIndex++));
		compileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.rayGenPushConstantsSize, hgValues.rayGenVariantCount, 
			hgValues.rayGenRayRecDepth, rayGenGroupIndex, ShaderStage::RayGeneration);
		if (compileResult) rayGenGroupIndex++;

		auto groupCompileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.missPushConstantsSize, hgValues.missVariantCount, 
			hgValues.missRayRecDepth, missGroupIndex, ShaderStage::Miss);
		compileResult |= groupCompileResult;
		if (groupCompileResult) missGroupIndex++;

		groupCompileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.callPushConstantsSize, hgValues.callVariantCount, 
			hgValues.callRayRecDepth, callGroupIndex, ShaderStage::Callable);
		compileResult |= groupCompileResult;
		if (groupCompileResult) callGroupIndex++;
		
		groupCompileResult = compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.intersectPushConstantsSize, hgValues.intersectVariantCount, 
			hgValues.intersectRayRecDepth, hitGroupIndex, ShaderStage::Intersection);
		groupCompileResult |= compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.anyHitPushConstantsSize, hgValues.anyHitVariantCount, 
			hgValues.anyHitRayRecDepth, hitGroupIndex, ShaderStage::AnyHit);
		groupCompileResult |= compileRayTracingShader(inputPath, outputPath, includePaths, 
			data, bindingIndex, hgValues.closHitPushConstantsSize, hgValues.closHitVariantCount, 
			hgValues.closHitRayRecDepth, hitGroupIndex, ShaderStage::ClosestHit);
		compileResult |= groupCompileResult;
		if (groupCompileResult) hitGroupIndex++;
		
		if (!compileResult)
			break;

		checkRtShaderValues(bValues, hgValues);
		if (hgValues.rayGenPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::RayGeneration;
		if (hgValues.missPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Miss;
		if (hgValues.callPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Callable;
		if (hgValues.intersectPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Intersection;
		if (hgValues.anyHitPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::AnyHit;
		if (hgValues.closHitPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::ClosestHit;
	}
	data.shaderPath.replace_extension();

	GARDEN_ASSERT(data.uniforms.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.samplerStates.size() <= UINT8_MAX);

	if (bValues.rayGenPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::RayGeneration;
	if (bValues.missPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Miss;
	if (bValues.callPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Callable;
	if (bValues.intersectPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::Intersection;
	if (bValues.anyHitPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::AnyHit;
	if (bValues.closHitPushConstantsSize > 0) data.pushConstantsStages |= ShaderStage::ClosestHit;
	
	data.pushConstantsSize = bValues.rayGenPushConstantsSize;
	data.variantCount = max(max(max(max(max(bValues.rayGenVariantCount, bValues.missVariantCount), 
		bValues.callVariantCount), bValues.intersectVariantCount), bValues.anyHitVariantCount), bValues.closHitVariantCount);
	data.rayRecursionDepth = max(max(max(max(max(bValues.rayGenRayRecDepth, bValues.missRayRecDepth), 
		bValues.callRayRecDepth), bValues.intersectRayRecDepth), bValues.anyHitRayRecDepth), bValues.closHitRayRecDepth);

	data.descriptorSetCount = 0;
	for (const auto& uniform : data.uniforms)
	{
		if (uniform.second.descriptorSetIndex > data.descriptorSetCount)
			data.descriptorSetCount = uniform.second.descriptorSetIndex;
	}
	data.descriptorSetCount++;

	RayTracingGslValues values;
	values.uniformCount = (uint8)data.uniforms.size();
	values.samplerStateCount = (uint8)data.samplerStates.size();
	values.descriptorSetCount = data.descriptorSetCount;
	values.variantCount = data.variantCount;
	values.pushConstantsSize = data.pushConstantsSize;
	values.specConstCount = (uint8)data.specConsts.size();
	values.pushConstantsStages = data.pushConstantsStages;
	values.rayRecursionDepth = data.rayRecursionDepth;
	
	ofstream headerStream;
	auto headerFilePath = outputPath / data.shaderPath; headerFilePath += ".gslh";
	writeGslHeaderValues(headerFilePath, rayTracingGslMagic, headerStream, values);
	
	writeGslHeaderArray<Pipeline::Uniform>(headerStream, data.uniforms);
	writeGslHeaderArray<Sampler::State>(headerStream, data.samplerStates);
	writeGslHeaderArray<Pipeline::SpecConst>(headerStream, data.specConsts);
	return true;
}
#endif

//******************************************************************************************************************
template<typename T>
static void readGslHeaderValues(const uint8* data, uint32 dataSize,
	uint32& dataOffset, string_view gslMagic, T& values)
{
	if (dataOffset + GslCompiler::gslMagicSize + sizeof(gslHeader) > dataSize)
		throw GardenError("Invalid GSL header size.");
	if (memcmp(data + dataOffset, gslMagic.data(), GslCompiler::gslMagicSize) != 0)
		throw GardenError("Invalid GSL header magic value.");
	dataOffset += GslCompiler::gslMagicSize;
	if (memcmp(data + dataOffset, gslHeader, sizeof(gslHeader)) != 0)
		throw GardenError("Invalid GSL header version or endianness.");
	dataOffset += sizeof(gslHeader);
	if (dataOffset + sizeof(T) > dataSize)
		throw GardenError("Invalid GSL header data size.");
	values = *(const T*)(data + dataOffset);
	dataOffset += sizeof(T);
}

template<typename T, typename A>
static void readGslHeaderArray(const uint8* data, uint32 dataSize, 
	uint32& dataOffset, uint8 count, A& valueArray)
{
	for (uint8 i = 0; i < count; i++)
	{
		if (dataOffset + sizeof(uint8) > dataSize)
			throw GardenError("Invalid GSL header data size.");
		auto nameLength = *(const uint8*)(data + dataOffset);
		dataOffset += sizeof(uint8);
		if (dataOffset + nameLength + sizeof(T) > dataSize)
			throw GardenError("Invalid GSL header data size.");
		string name(nameLength, ' ');
		memcpy(name.data(), data + dataOffset, nameLength);
		dataOffset += nameLength;
		const auto& value = *(const T*)(data + dataOffset);
		dataOffset += sizeof(T);
		if (!valueArray.emplace(std::move(name), value).second)
			throw GardenError("Invalid GSL header data.");
	}
}

//******************************************************************************************************************
void GslCompiler::loadGraphicsShaders(GraphicsData& data)
{
	if (!data.shaderPath.empty())
	{
		auto shadersPath = "shaders" / data.shaderPath;
		auto headerPath = shadersPath; headerPath += ".gslh";
		auto vertexPath = shadersPath; vertexPath += ".vert.spv";
		auto fragmentPath = shadersPath; fragmentPath += ".frag.spv";

		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		auto threadIndex = data.threadIndex < 0 ? 0 : data.threadIndex + 1; uint64 itemIndex = 0;
		data.packReader->readItemData(headerPath, data.headerData, threadIndex);
		data.packReader->readItemData(vertexPath, data.vertexCode, threadIndex);
		if (data.packReader->getItemIndex(fragmentPath, itemIndex))
			data.packReader->readItemData(itemIndex, data.fragmentCode, threadIndex);
		#else
		File::loadBinary(data.cachePath / headerPath, data.headerData);
		File::loadBinary(data.cachePath / vertexPath, data.vertexCode);
		if (fs::exists(data.cachePath / fragmentPath)) // Note: It's allowed to have only vertex shader.
			File::loadBinary(data.cachePath / fragmentPath, data.fragmentCode);
		#endif
	}
	
	GraphicsGslValues values; uint32 dataOffset = 0;
	auto headerData = data.headerData.data(); auto dataSize = (uint32)data.headerData.size();
	readGslHeaderValues(headerData, dataSize, dataOffset, graphicsGslMagic, values);

	if (dataOffset + values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute) +
		values.blendStateCount * sizeof(GraphicsPipeline::BlendState) > dataSize)
	{
		throw GardenError("Invalid GSL header data size.");
	}

	if (values.vertexAttributeCount > 0)
	{
		data.vertexAttributes.resize(values.vertexAttributeCount);
		memcpy(data.vertexAttributes.data(), headerData + dataOffset,
			values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute));
		dataOffset += values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute);
	}
	if (values.blendStateCount > 0)
	{
		data.blendStates.resize(values.blendStateCount);
		memcpy(data.blendStates.data(), headerData + dataOffset,
			values.blendStateCount * sizeof(GraphicsPipeline::BlendState));
		dataOffset += values.blendStateCount * sizeof(GraphicsPipeline::BlendState);
	}

	readGslHeaderArray<Pipeline::Uniform>(headerData, dataSize, 
		dataOffset, values.uniformCount, data.uniforms);
	readGslHeaderArray<Sampler::State>(headerData, dataSize, 
		dataOffset, values.samplerStateCount, data.samplerStates);
	readGslHeaderArray<Pipeline::SpecConst>(headerData, dataSize, 
		dataOffset, values.specConstCount, data.specConsts);

	data.pushConstantsStages = values.pushConstantsStages;
	data.pushConstantsSize = values.pushConstantsSize;
	data.descriptorSetCount = values.descriptorSetCount;
	data.variantCount = values.variantCount;
	data.pipelineState = values.pipelineState;
	data.vertexAttributesSize = values.vertexAttributesSize;
}

//******************************************************************************************************************
void GslCompiler::loadComputeShader(ComputeData& data)
{
	if (!data.shaderPath.empty())
	{
		auto shadersPath = "shaders" / data.shaderPath;
		auto headerPath = shadersPath; headerPath += ".gslh";
		auto computePath = shadersPath; computePath += ".comp.spv";

		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		auto threadIndex = data.threadIndex < 0 ? 0 : data.threadIndex + 1;
		data.packReader->readItemData(headerPath, data.headerData, threadIndex);
		data.packReader->readItemData(computePath, data.code, threadIndex);
		#else
		File::loadBinary(data.cachePath / headerPath, data.headerData);
		File::loadBinary(data.cachePath / computePath, data.code);
		#endif
	}

	ComputeGslValues values; uint32 dataOffset = 0; 
	auto headerData = data.headerData.data(); auto dataSize = (uint32)data.headerData.size();
	readGslHeaderValues(headerData, dataSize, dataOffset, computeGslMagic, values);

	readGslHeaderArray<Pipeline::Uniform>(headerData, dataSize, 
		dataOffset, values.uniformCount, data.uniforms);
	readGslHeaderArray<Sampler::State>(headerData, dataSize, 
		dataOffset, values.samplerStateCount, data.samplerStates);
	readGslHeaderArray<Pipeline::SpecConst>(headerData, dataSize, 
		dataOffset, values.specConstCount, data.specConsts);

	if (values.pushConstantsSize > 0)
		data.pushConstantsStages = ShaderStage::Compute;

	data.pushConstantsSize = values.pushConstantsSize;
	data.descriptorSetCount = values.descriptorSetCount;
	data.variantCount = values.variantCount;
	data.localSize = values.localSize;
}

//******************************************************************************************************************
void GslCompiler::loadRayTracingShaders(RayTracingData& data)
{
	if (!data.shaderPath.empty())
	{
		auto shadersPath = "shaders" / data.shaderPath;
		auto headerPath = shadersPath; headerPath += ".gslh";
		auto rayGenerationPath = shadersPath; rayGenerationPath += ".rgen.spv";
		auto missPath = shadersPath; missPath += ".rmiss.spv";
		auto intersectionPath = shadersPath; intersectionPath += ".rint.spv";
		auto anyHitPath = shadersPath; anyHitPath += ".rahit.spv";
		auto closestHitPath = shadersPath; closestHitPath += ".rchit.spv";
		auto callablePath = shadersPath; callablePath += ".rcall.spv";
		data.rayGenGroups.resize(1); data.missGroups.resize(1); data.hitGroups.resize(1);

		#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
		auto threadIndex = data.threadIndex < 0 ? 0 : data.threadIndex + 1; uint64 itemIndex = 0;
		data.packReader->readItemData(headerPath, data.headerData, threadIndex);
		data.packReader->readItemData(rayGenerationPath, data.rayGenGroups[0], threadIndex);
		data.packReader->readItemData(missPath, data.missGroups[0], threadIndex);
		#else
		File::loadBinary(data.cachePath / headerPath, data.headerData);
		File::loadBinary(data.cachePath / rayGenerationPath, data.rayGenGroups[0]);
		File::loadBinary(data.cachePath / missPath, data.missGroups[0]);
		#endif

		for (uint8 i = 1; i < UINT8_MAX - 1; i++)
		{
			rayGenerationPath = shadersPath; rayGenerationPath += "." + to_string(i) + ".rgen.spv";

			#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
			if (data.packReader->getItemIndex(rayGenerationPath, itemIndex))
			{
				data.rayGenGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.rayGenGroups[i], threadIndex);
			}
			else break;
			#else
			if (fs::exists(data.cachePath / rayGenerationPath))
			{
				data.rayGenGroups.push_back({});
				File::loadBinary(data.cachePath / rayGenerationPath, data.rayGenGroups[i]);
			}
			else break;
			#endif
		}
		for (uint8 i = 1; i < UINT8_MAX - 1; i++)
		{
			missPath = shadersPath; missPath += "." + to_string(i) + ".rmiss.spv";

			#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
			if (data.packReader->getItemIndex(missPath, itemIndex))
			{
				data.missGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.missGroups[i], threadIndex);
			}
			else break;
			#else
			if (fs::exists(data.cachePath / missPath))
			{
				data.missGroups.push_back({});
				File::loadBinary(data.cachePath / missPath, data.missGroups[i]);
			}
			else break;
			#endif
		}
		for (uint8 i = 0; i < UINT8_MAX - 1; i++)
		{
			#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
			if (data.packReader->getItemIndex(callablePath, itemIndex))
			{
				data.callGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.callGroups[i], threadIndex);
			}
			else break;
			#else
			if (fs::exists(data.cachePath / callablePath))
			{
				data.callGroups.push_back({});
				File::loadBinary(data.cachePath / callablePath, data.callGroups[i]);
			}
			else break;
			#endif

			callablePath = shadersPath; callablePath += "." + to_string(i + 1) + ".rcall.spv";
		}

		for (uint8 i = 0; i < UINT8_MAX - 1; i++)
		{
			auto anyLoaded = false;

			#if GARDEN_PACK_RESOURCES && !defined(GSL_COMPILER)
			if (data.packReader->getItemIndex(intersectionPath, itemIndex))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.hitGroups[i].intersectionCode, threadIndex);
				anyLoaded = true;
			}
			if (data.packReader->getItemIndex(anyHitPath, itemIndex))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.hitGroups[i].anyHitCode, threadIndex);
				anyLoaded = true;
			}
			if (data.packReader->getItemIndex(closestHitPath, itemIndex))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				data.packReader->readItemData(itemIndex, data.hitGroups[i].closestHitCode, threadIndex);
				anyLoaded = true;
			}
			#else
			if (fs::exists(data.cachePath / intersectionPath))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				File::loadBinary(data.cachePath / intersectionPath, data.hitGroups[i].intersectionCode);
				anyLoaded = true;
			}
			if (fs::exists(data.cachePath / anyHitPath))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				File::loadBinary(data.cachePath / anyHitPath, data.hitGroups[i].anyHitCode);
				anyLoaded = true;
			}
			if (fs::exists(data.cachePath / closestHitPath))
			{
				if (data.hitGroups.size() <= i) data.hitGroups.push_back({});
				File::loadBinary(data.cachePath / closestHitPath, data.hitGroups[i].closestHitCode);
				anyLoaded = true;
			}
			#endif

			if (!anyLoaded)
				break;

			intersectionPath = shadersPath; intersectionPath += "." + to_string(i + 1) + ".rint.spv";
			anyHitPath = shadersPath; anyHitPath += "." + to_string(i + 1) + ".rahit.spv";
			closestHitPath = shadersPath; closestHitPath += "." + to_string(i + 1) + ".rchit.spv";
		}
	}

	if (data.hitGroups[0].anyHitCode.empty() && data.hitGroups[0].closestHitCode.empty())
		throw GardenError("No ray tracing hit shader in the first group.");
	
	RayTracingGslValues values; uint32 dataOffset = 0;
	auto headerData = data.headerData.data(); auto dataSize = (uint32)data.headerData.size();
	readGslHeaderValues(headerData, dataSize, dataOffset, rayTracingGslMagic, values);

	readGslHeaderArray<Pipeline::Uniform>(headerData, dataSize, 
		dataOffset, values.uniformCount, data.uniforms);
	readGslHeaderArray<Sampler::State>(headerData, dataSize, 
		dataOffset, values.samplerStateCount, data.samplerStates);
	readGslHeaderArray<Pipeline::SpecConst>(headerData, dataSize, 
		dataOffset, values.specConstCount, data.specConsts);

	data.rayRecursionDepth = values.rayRecursionDepth;
	data.pushConstantsStages = values.pushConstantsStages;
	data.pushConstantsSize = values.pushConstantsSize;
	data.descriptorSetCount = values.descriptorSetCount;
	data.variantCount = values.variantCount;
}

#ifdef GSL_COMPILER
//******************************************************************************************************************
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "gslc: error: no shader file name\n";
		return EXIT_FAILURE;
	}

	vector<fs::path> includePaths; int logOffset = 1;
	fs::path workingPath = fs::path(argv[0]).parent_path();
	auto inputPath = workingPath, outputPath = workingPath;
	ThreadPool* threadPool = nullptr; atomic_int compileResult = true;
	
	for (int i = 1; i < argc; i++)
	{
		auto arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
		{
			cout << "(C) 2022-" GARDEN_CURRENT_YEAR " Nikita Fediuchin. All rights reserved.\n"
				"gslc - Garden Shading Language Compiler (GLSL dialect)\n"
				"\n"
				"Usage: gslc [options] name...\n"
				"\n"
				"Options:\n"
				"  -i <dir>      Read input from <dir>.\n"
				"  -o <dir>      Write output to <dir>.\n"
				"  -I <value>    Add directory to include search path.\n"
				"  -t <value>    Specify thread pool size. (Uses all cores by default)\n"
				"  -h            Display available options.\n"
				"  --help        Display available options.\n"
				"  --version     Display compiler version information.\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "--version") == 0)
		{
			cout << "gslc " GARDEN_VERSION_STRING "\n";
			return EXIT_SUCCESS;
		}
		else if (strcmp(arg, "-i") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no input directory\n";
				return EXIT_FAILURE;
			}

			inputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no output directory\n";
				return EXIT_FAILURE;
			}

			outputPath = argv[i + 1];
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-I") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no include directory\n";
				return EXIT_FAILURE;
			}

			includePaths.push_back(argv[i + 1]);
			logOffset += 2; i++;
		}
		else if (strcmp(arg, "-t") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no thread count\n";
				return EXIT_FAILURE;
			}

			auto count = atoi(argv[i + 1]);
			if (count > 0 && count < thread::hardware_concurrency())
			{
				if (threadPool)
				{
					threadPool->wait();
					delete threadPool;
				}
				threadPool = new ThreadPool(false, "T", count);
			}
			logOffset += 2; i++;
		}
		else if (arg[0] == '-')
		{
			cout << "gslc: error: unsupported option: '" << arg << "'\n";
			return EXIT_FAILURE;
		}
		else
		{
			if (!threadPool)
				threadPool = new ThreadPool(false, "T");
			threadPool->addTask([=, &compileResult](const ThreadPool::Task& task)
			{
				if (!compileResult)
					return;

				// Note: Sending one batched message due to multithreading.
				cout << string("Compiling ") + arg + "\n" << flush;
				auto result = false;

				try
				{
					GslCompiler::GraphicsData graphicsData;
					graphicsData.shaderPath = arg;
					result = GslCompiler::compileGraphicsShaders(inputPath, outputPath, includePaths, graphicsData);
				}
				catch (const exception& e)
				{
					if (strcmp(e.what(), "_GLSLC") != 0)
						cout << string(e.what()) + "\n";
				}
	
				try
				{
					GslCompiler::ComputeData computeData;
					computeData.shaderPath = arg;
					result |= GslCompiler::compileComputeShader(inputPath, outputPath, includePaths, computeData);
				}
				catch (const exception& e)
				{
					if (strcmp(e.what(), "_GLSLC") != 0)
						cout << string(e.what()) + '\n';
				}

				try
				{
					GslCompiler::RayTracingData rayTracingData;
					rayTracingData.shaderPath = arg;
					result |= GslCompiler::compileRayTracingShaders(inputPath, outputPath, includePaths, rayTracingData);
				}
				catch (const exception& e)
				{
					if (strcmp(e.what(), "_GLSLC") != 0)
						cout << string(e.what()) + '\n';
				}

				if (!result)
					cout << string("gslc: error: no shader files found (") + arg + ")\n";
				compileResult &= result;
			});
		}
	}

	if (threadPool)
	{
		threadPool->wait();
		delete threadPool;
	}
	return compileResult ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
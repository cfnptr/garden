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

#include "garden/graphics/compiler.hpp"
#include "garden/file.hpp"

#include <fstream>

#if __APPLE__
#define GARDEN_VULKAN_SHADER_VERSION_STRING "vulkan1.2"
#else
#define GARDEN_VULKAN_SHADER_VERSION_STRING "vulkan1.3"
#endif

using namespace std;
using namespace garden;
using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
static const uint8 gslHeader[] = { 1, 0, 0, GARDEN_LITTLE_ENDIAN, };
static const string_view graphicsGslMagic = "GSLG";
static const string_view computeGslMagic = "GSLC";
#define GSL_MAGIC_SIZE 4

namespace
{
	struct GslValues
	{
		uint8 uniformCount = 0;
		uint8 samplerStateCount = 0;
		uint8 descriptorSetCount = 0;
		uint8 variantCount = 0;
		uint16 pushConstantsSize = 0;
	};
	struct GraphicsGslValues final : public GslValues
	{
		uint16 vertexAttributesSize = 0;
		uint8 vertexAttributeCount = 0;
		uint8 blendStateCount = 0;
		ShaderStage pushConstantsStages = {};
		uint8 _alignmnet1 = 0;
		GraphicsPipeline::State pipelineState;
	// should be algined.
	};
	struct ComputeGslValues final : public GslValues
	{
		uint16 _alignmnet1 = 0;
		int3 localSize = int3(0);
		// should be algined.
	};
}

#if GARDEN_DEBUG || defined(GSL_COMPILER)
//--------------------------------------------------------------------------------------------------
namespace
{
	struct FileData
	{
		ifstream inputFileStream; ofstream outputFileStream;
		istringstream inputStream; ostringstream outputStream;
		string line; uint32 lineIndex = 1;
		int8 isUniform = 0, isBuffer = 0, isPushConstants = 0, isSamplerState = 0;
		bool isSkipMode = false, isReadonly = false, isWriteonly = false,
			isRestrict = false, isVolatile = false, isCoherent = false;
		uint8 attachmentIndex = 0, descriptorSetIndex = 0;
		GslUniformType uniformType = {};
		Pipeline::SamplerState samplerState; 
	};
	struct GraphicsFileData final : public FileData
	{
		int8 inIndex = 0, outIndex = 0, isPipelineState = 0;
		uint8 blendStateIndex = 0;
	};
	struct ComputeFileData final : public FileData { };

//--------------------------------------------------------------------------------------------------
	struct LineData
	{
		string word, uniformName;
		int8 isComparing = 0, isCompareOperation = 0, isAnisoFiltering = 0, isUnnormCoords = 0,
			isFilter = 0, isFilterMin = 0, isFilterMag = 0, isFilterMipmap = 0,
			isBorderColor = 0, isWrap = 0, isWrapX = 0, isWrapY = 0, isWrapZ = 0,
			isFeature = 0, isVariantCount = 0;
		uint8 arraySize = 1;
		GslDataType dataType = {}; GslImageFormat imageFormat = {};
		bool isNewLine = true;
	};
	struct GraphicsLineData final : public LineData
	{
		int8 isIn = 0, isOut = 0, isTopology = 0, isPolygon = 0, isDiscarding = 0,
			isDepthTesting = 0, isDepthWriting = 0, isDepthClamping = 0,
			isDepthBiasing = 0, isDepthCompare = 0, isDepthOverride = 0,
			isFaceCulling = 0, isCullFace = 0, isFrontFace = 0,
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

	class CompileError final : public runtime_error
	{
	public:
		CompileError(const string& message, int32 line = -1, const string& word = "") :
			runtime_error(
			line < 0 ? "error:" + (word.length() > 0 ?
				"'" + word + "' : " : " ") + message :
			to_string(line) + ": error:" + (word.length() > 0 ?
				"'" + word + "' : " : " ") + message) { }
	};
}

//--------------------------------------------------------------------------------------------------
static bool toBoolState(string_view state, uint32 lineIndex)
{
	if (state == "on") return true;
	if (state == "off") return false;
	throw CompileError("unrecognized boolean state", lineIndex, string(state));
}
static string_view toStringState(bool state)
{
	return state ? "on" : "off";
} 

static GraphicsPipeline::ColorComponent toColorComponents(string_view colorComponents)
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

//--------------------------------------------------------------------------------------------------
static const string_view glslImageFormatNames[(psize)GslImageFormat::Count] =
{
	"rgba16f", "rgba32f", "rg16f", "rg32f", "r16f", "r32f",
	"rgba8i", "rgba16i", "rgba32i", "rg8i", "rg16i", "rg32i", "r8i", "r16i", "r32i",
	"rgba8ui", "rgba16ui", "rgba32ui", "rg8ui", "rg16ui", "rg32ui", "r8ui", "r16ui", "r32ui"
};

static string_view toGlslString(GslImageFormat imageFormat)
{
	GARDEN_ASSERT((uint8)imageFormat < (uint8)GslImageFormat::Count);
	return glslImageFormatNames[(psize)imageFormat];
}

//--------------------------------------------------------------------------------------------------
static void onShaderUniform(
	FileData& fileData, LineData& lineData, ShaderStage shaderStage,
	uint8& bindingIndex, map<string, Pipeline::Uniform>& uniforms,
	map<string, Pipeline::SamplerState>& samplerStates)
{
	if (fileData.isUniform == 1)
	{
		if (lineData.word == "readonly") { fileData.isReadonly = true; }
		else if (lineData.word == "writeonly") { fileData.isWriteonly = true; }
		else if (lineData.word == "restrict") { fileData.isRestrict = true; }
		else if (lineData.word == "volatile") { fileData.isVolatile = true; }
		else if (lineData.word == "coherent") { fileData.isCoherent = true; }
		else if (fileData.isBuffer)
		{
			fileData.uniformType = GslUniformType::StorageBuffer;
			fileData.outputStream.str(""); fileData.outputStream.clear();
			fileData.outputStream << lineData.word << " ";
			fileData.isBuffer = 0; fileData.isUniform = 4;
		}
		else if (lineData.word == toString(GslUniformType::PushConstants))
		{
			fileData.outputFileStream << "layout(push_constant) uniform pushConstants ";
			fileData.isUniform = 0; fileData.isPushConstants = 1;
		}
		else if (lineData.word == toString(GslUniformType::SubpassInput))
		{
			fileData.uniformType = GslUniformType::SubpassInput; fileData.isUniform = 3;
		}
		else if (lineData.word.length() > 3 && memcmp(lineData.word.data(), "set", 3) == 0)
		{
			auto index = strtol(lineData.word.c_str() + 3, nullptr, 10);
			if (index < 0 || index > UINT8_MAX)
			{
				throw CompileError("invalid descriptor set index",
					fileData.lineIndex, lineData.word);
			}
			fileData.descriptorSetIndex = (uint8)index;
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
		if (fileData.uniformType == GslUniformType::UniformBuffer ||
			fileData.uniformType == GslUniformType::StorageBuffer)
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
		{
			throw CompileError("no uniform image format after name",
				fileData.lineIndex, lineData.uniformName);
		}

		auto arrayOpenPos = lineData.uniformName.find_first_of('[');
		if (arrayOpenPos != string::npos)
		{
			auto arraySize = strtol(lineData.uniformName.c_str() +
				arrayOpenPos + 1, nullptr, 10);
			if (arraySize == 0)
			{
				auto arrayClosePos = lineData.uniformName.find_first_of(']', arrayOpenPos);
				if (arrayClosePos == string::npos)
				{
					throw CompileError("no ']' after uniform array size",
						fileData.lineIndex, lineData.uniformName);
				}
				if (arrayClosePos - arrayOpenPos != 1)
				{
					throw CompileError("invalid uniform array size",
						fileData.lineIndex, lineData.uniformName);
				}
			}
			lineData.uniformName.resize(arrayOpenPos);
			lineData.arraySize = arraySize;
		}
		
		fileData.isUniform = 6;
		return;
	}
	else if (fileData.isUniform == 6)
	{
		if (lineData.word != ":")
		{
			throw CompileError("no ':' after uniform image format",
				fileData.lineIndex, lineData.word);
		}
		fileData.isUniform = 7;
		return;
	}
	else if (fileData.isUniform == 7)
	{
		auto format = string_view(lineData.word.c_str(),
			lineData.word.find_first_of(';'));
		try { lineData.imageFormat = toGslImageFormat(format); }
		catch (const exception&)
		{
			throw CompileError("unrecognized uniform image format",
				fileData.lineIndex, string(format));
		}
	}

	if (lineData.uniformName.empty())
	{
		lineData.uniformName = string(lineData.word, 0,
			lineData.word.find_first_of(';'));
		if (lineData.word.length() == lineData.uniformName.length())
		{
			throw CompileError("no ';' after uniform name",
				fileData.lineIndex, lineData.uniformName);
		}

		auto arrayOpenPos = lineData.uniformName.find_first_of('[');
		if (arrayOpenPos != string::npos)
		{
			auto arraySize = strtol(lineData.uniformName.c_str() +
				arrayOpenPos + 1, nullptr, 10);
			if (arraySize == 0)
			{
				auto arrayClosePos = lineData.uniformName.find_first_of(']', arrayOpenPos);
				if (arrayClosePos == string::npos)
				{
					throw CompileError("no ']' after uniform array size",
						fileData.lineIndex, lineData.uniformName);
				}
				if (arrayClosePos - arrayOpenPos != 1)
				{
					throw CompileError("invalid uniform array size",
						fileData.lineIndex, lineData.uniformName);
				}
			}
			lineData.uniformName.resize(arrayOpenPos);
			lineData.arraySize = arraySize;
		}
	}

	auto writeAccess = !fileData.isReadonly && !isSamplerType(fileData.uniformType) &&
		fileData.uniformType != GslUniformType::UniformBuffer &&
		fileData.uniformType != GslUniformType::SubpassInput &&
		fileData.uniformType != GslUniformType::PushConstants;
	auto readAccess = !fileData.isWriteonly;

	if (!readAccess && !writeAccess)
	{
		throw CompileError("no uniform read and write access",
			fileData.lineIndex, lineData.uniformName);
	}

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
		auto result = uniforms.emplace(lineData.uniformName, uniform);
		if (!result.second) throw runtime_error("Failed to emplace uniform.");
	}
	else
	{
		auto& uniform = uniforms.at(lineData.uniformName);
		if (hasAnyFlag(uniform.shaderStages, shaderStage))
		{
			throw CompileError("duplicate uniform name",
				fileData.lineIndex, lineData.uniformName);
		}
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
		uniform.shaderStages |= shaderStage;
		binding = uniform.bindingIndex;
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
			", set = " << to_string(fileData.descriptorSetIndex) <<
			") uniform " << fileData.outputStream.str();
	}
	else if (fileData.uniformType == GslUniformType::StorageBuffer)
	{
		fileData.outputFileStream << "layout(binding = " << to_string(binding) <<
			", set = " << to_string(fileData.descriptorSetIndex) << ") ";
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
		if (lineData.arraySize > 0)
			fileData.outputFileStream << "[" << (int)lineData.arraySize << "]";
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
			if (memcmp(&fileData.samplerState, &result->second,
				sizeof(Pipeline::SamplerState)) != 0)
			{
				throw CompileError("different sampler state with the same name",
					fileData.lineIndex, lineData.uniformName);
			}
		}

		if (fileData.isUniform == 3)
		{
			fileData.outputFileStream << fileData.outputStream.str();
			fileData.samplerState = {};
		}
	}

	fileData.isReadonly = fileData.isWriteonly = fileData.isRestrict =
		fileData.isVolatile = fileData.isCoherent = false;
	fileData.isUniform = fileData.descriptorSetIndex = 0;
}

//--------------------------------------------------------------------------------------------------
static void onShaderPushConstants(FileData& fileData,
	LineData& lineData, uint16& pushConstantsSize)
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
			{
				throw CompileError("unrecognized push constant "
					"GSL data type", fileData.lineIndex, lineData.word);
			}
			fileData.isPushConstants = 3;
		}
	}
	else
	{
		pushConstantsSize += (uint16)toBinarySize(lineData.dataType);
		fileData.isPushConstants = 2;
		if (pushConstantsSize > GARDEN_MAX_PUSH_CONSTANTS_SIZE)
			throw CompileError("out of max push constants size", fileData.lineIndex);
	}
}

//--------------------------------------------------------------------------------------------------
static SamplerFilter toSamplerFilter(string_view name, uint32 lineIndex)
{
	try { return toSamplerFilter(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized sampler filter type", lineIndex, string(name));
	}
}
static Pipeline::SamplerWrap toSamplerWrap(string_view name, uint32 lineIndex)
{
	try { return toSamplerWrap(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized sampler wrap type", lineIndex, string(name));
	}
}
static Pipeline::BorderColor toBorderColor(string_view name, uint32 lineIndex)
{
	try { return toBorderColor(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized border color type", lineIndex, string(name));
	}
}
static Pipeline::CompareOperation toCompareOperation(string_view name, uint32 lineIndex)
{
	try { return toCompareOperation(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized compare operation type", lineIndex, string(name));
	}
}

//--------------------------------------------------------------------------------------------------
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
			if (lineData.word == "filter") lineData.isFilter = 1;
			else if (lineData.word == "filterMin") lineData.isFilterMin = 1;
			else if (lineData.word == "filterMag") lineData.isFilterMag = 1;
			else if (lineData.word == "filterMipmap") lineData.isFilterMipmap = 1;
			else if (lineData.word == "borderColor") lineData.isBorderColor = 1;
			else if (lineData.word == "wrap") lineData.isWrap = 1;
			else if (lineData.word == "wrapX") lineData.isWrapX = 1;
			else if (lineData.word == "wrapY") lineData.isWrapY = 1;
			else if (lineData.word == "wrapZ") lineData.isWrapZ = 1;
			else if (lineData.word == "comparing") lineData.isComparing = 1;
			else if (lineData.word == "compareOperation") lineData.isCompareOperation = 1;
			else if (lineData.word == "anisoFiltering") lineData.isAnisoFiltering = 1;
			else if (lineData.word == "unnormCoords") lineData.isUnnormCoords = 1;
			else
			{
				throw CompileError("unrecognized sampler property",
					fileData.lineIndex, lineData.word);
			}
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
		{
			throw CompileError("no ';' after sampler property name",
				fileData.lineIndex, string(name));
		}

		if (lineData.isFilter)
		{
			fileData.samplerState.magFilter = fileData.samplerState.minFilter =
				fileData.samplerState.mipmapFilter =
				toSamplerFilter(name, fileData.lineIndex);
			lineData.isFilter = 0;
		}
		else if (lineData.isFilterMin)
		{
			fileData.samplerState.minFilter = toSamplerFilter(name, fileData.lineIndex);
			lineData.isFilterMin = 0;
		}
		else if (lineData.isFilterMag)
		{
			fileData.samplerState.magFilter = toSamplerFilter(name,fileData.lineIndex);
			lineData.isFilterMag = 0;
		}
		else if (lineData.isFilterMipmap)
		{
			fileData.samplerState.mipmapFilter = toSamplerFilter(name, fileData.lineIndex);
			lineData.isFilterMag = 0;
		}
		else if (lineData.isBorderColor)
		{
			fileData.samplerState.borderColor = toBorderColor(name, fileData.lineIndex);
			lineData.isBorderColor = 0;
		}
		else if (lineData.isWrap)
		{
			fileData.samplerState.wrapX = fileData.samplerState.wrapY =
				fileData.samplerState.wrapZ = toSamplerWrap(name, fileData.lineIndex);
			lineData.isWrap = 0;
		}
		else if (lineData.isWrapX)
		{
			fileData.samplerState.wrapX = toSamplerWrap(name, fileData.lineIndex);
			lineData.isWrapX = 0;
		}
		else if (lineData.isWrapY)
		{
			fileData.samplerState.wrapY = toSamplerWrap(name, fileData.lineIndex);
			lineData.isWrapY = 0;
		}
		else if (lineData.isWrapZ)
		{
			fileData.samplerState.wrapZ = toSamplerWrap(name, fileData.lineIndex);
			lineData.isWrapZ = 0;
		}
		else if (lineData.isComparing)
		{
			fileData.samplerState.comparing = toBoolState(name, fileData.lineIndex);
			lineData.isComparing = 0;
		}
		else if (lineData.isCompareOperation)
		{
			fileData.samplerState.compareOperation =
				toCompareOperation(name, fileData.lineIndex);
			lineData.isCompareOperation = 0;
		}
		else if (lineData.isAnisoFiltering)
		{
			fileData.samplerState.anisoFiltering = toBoolState(name, fileData.lineIndex);
			lineData.isAnisoFiltering = 0;
		}
		else if (lineData.isUnnormCoords)
		{
			fileData.samplerState.unnormCoords = toBoolState(name, fileData.lineIndex);
			lineData.isUnnormCoords = 0;
		}
		else abort();

		fileData.outputStream << lineData.word << "\n"; fileData.isSamplerState = 1;
	}
}

//--------------------------------------------------------------------------------------------------
static GraphicsPipeline::BlendFactor toBlendFactor(string_view name, uint32 lineIndex)
{
	try { return toBlendFactor(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized pipeline state "
			"blend factor type", lineIndex, string(name));
	}
}
static GraphicsPipeline::BlendOperation toBlendOperation(
	string_view name, uint32 lineIndex)
{
	try { return toBlendOperation(name); }
	catch (const exception&)
	{
		throw CompileError("unrecognized pipeline state "
			"blend operation type", lineIndex, string(name));
	}
}

//--------------------------------------------------------------------------------------------------
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
			else if (lineData.word == "faceCulling") lineData.isFaceCulling = 1;
			else if (lineData.word == "cullFace") lineData.isCullFace = 1;
			else if (lineData.word == "frontFace") lineData.isFrontFace = 1;
			else if (lineData.word.length() >= 9 &&
				memcmp(lineData.word.c_str(), "colorMask", 9) == 0)
			{
				if (lineData.word.length() > 9) // TODO: check strtol for overflow
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 9, nullptr, 10);
				else throw CompileError("no colorMask blend state index", fileData.lineIndex);
				lineData.isColorMask = 1;
			}
			else if (lineData.word.length() >= 8 &&
				memcmp(lineData.word.c_str(), "blending", 8) == 0)
			{
				if (lineData.word.length() > 8)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 8, nullptr, 10);
				else throw CompileError("no blending blend state index", fileData.lineIndex);
				lineData.isBlending = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "srcBlendFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no src blend factor blend state index", fileData.lineIndex);
				lineData.isSrcBlendFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "dstBlendFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no dst blend factor blend state index", fileData.lineIndex);
				lineData.isDstBlendFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "srcColorFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no src color factor blend state index", fileData.lineIndex);
				lineData.isSrcColorFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "dstColorFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no dst color factor blend state index", fileData.lineIndex);
				lineData.isDstColorFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "srcAlphaFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no src alpha factor blend state index", fileData.lineIndex);
				lineData.isSrcAlphaFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "dstAlphaFactor", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no dst alpha factor blend state index", fileData.lineIndex);
				lineData.isDstAlphaFactor = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "blendOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no blend operation blend state index", fileData.lineIndex);
				lineData.isBlendOperation = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "colorOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no color operation blend state index", fileData.lineIndex);
				lineData.isColorOperation = 1;
			}
			else if (lineData.word.length() >= 14 &&
				memcmp(lineData.word.c_str(), "alphaOperation", 14) == 0)
			{
				if (lineData.word.length() > 14)
					fileData.blendStateIndex = (uint8)strtol(lineData.word.c_str() + 14, nullptr, 10);
				else throw CompileError("no alpha operation blend state index", fileData.lineIndex);
				lineData.isAlphaOperation = 1;
			}
			else
			{
				throw CompileError("unrecognized pipeline state property",
					fileData.lineIndex, lineData.word);
			}
			fileData.outputFileStream << "//     " << lineData.word << " ";
			fileData.isPipelineState = 3;
		}
	}
	else if (fileData.isPipelineState == 3)
	{
		if (lineData.word != "=")
		{
			throw CompileError("no '=' after pipeline state "
				"property name", fileData.lineIndex);
		}
		fileData.outputFileStream << "= "; fileData.isPipelineState = 4;
	}
	else
	{
		auto name = string_view(lineData.word.c_str(),
			lineData.word.find_first_of(';'));
		if (lineData.word.length() == name.length())
		{
			throw CompileError("no ';' after pipeline state property name",
				fileData.lineIndex, string(name));
		}

		if (lineData.isTopology)
		{
			try { state.topology = toTopology(name); }
			catch (const exception&)
			{
				throw CompileError("unrecognized pipeline state "
					"topology type", fileData.lineIndex, string(name));
			}
			lineData.isTopology = 0;
		}
		else if (lineData.isPolygon)
		{
			try { state.polygon = toPolygon(name); }
			catch (const exception&)
			{
				throw CompileError("unrecognized pipeline state "
					"polygon type", fileData.lineIndex, string(name));
			}
			lineData.isPolygon = 0;
		}
		else if (lineData.isDiscarding)
		{
			state.discarding = toBoolState(name, fileData.lineIndex);
			lineData.isDiscarding = 0; 
		}
		else if (lineData.isDepthTesting)
		{
			state.depthTesting = toBoolState(name, fileData.lineIndex);
			lineData.isDepthTesting = 0; 
		}
		else if (lineData.isDepthWriting)
		{
			state.depthWriting = toBoolState(name, fileData.lineIndex);
			lineData.isDepthWriting = 0;
		}
		else if (lineData.isDepthClamping)
		{
			state.depthClamping = toBoolState(name, fileData.lineIndex);
			lineData.isDepthClamping = 0;
		}
		else if (lineData.isDepthBiasing)
		{
			state.depthBiasing = toBoolState(name, fileData.lineIndex);
			lineData.isDepthBiasing = 0;
		}
		else if (lineData.isDepthCompare)
		{
			state.depthCompare = toCompareOperation(name, fileData.lineIndex);
			lineData.isDepthCompare = 0;
		}
		else if (lineData.isFaceCulling)
		{
			state.faceCulling = toBoolState(name, fileData.lineIndex);
			lineData.isFaceCulling = 0;
		}
		else if (lineData.isCullFace)
		{
			try { state.cullFace = toCullFace(name); }
			catch (const exception&)
			{
				throw CompileError("unrecognized pipeline state "
					"cull face type", fileData.lineIndex, string(name));
			}
			lineData.isCullFace = 0;
		}
		else if (lineData.isFrontFace)
		{
			try { state.frontFace = toFrontFace(name); }
			catch (const exception&)
			{
				throw CompileError("unrecognized pipeline state "
					"front face type", fileData.lineIndex, string(name));
			}
			lineData.isFrontFace = 0;
		}
		else if (lineData.isColorMask)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].colorMask =
				toColorComponents(lineData.word);
			lineData.isColorMask = 0;
		}
		else if (lineData.isBlending)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].blending =
				toBoolState(name, fileData.lineIndex);
			lineData.isBlending = 0;
		}
		else if (lineData.isSrcBlendFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcColorFactor =
			blendStates[fileData.blendStateIndex].srcAlphaFactor =
				toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcBlendFactor = 0;
		}
		else if (lineData.isDstBlendFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstColorFactor =
			blendStates[fileData.blendStateIndex].dstAlphaFactor =
				toBlendFactor(name, fileData.lineIndex);
			lineData.isDstBlendFactor = 0;
		}
		else if (lineData.isSrcColorFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcColorFactor =
				toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcColorFactor = 0;
		}
		else if (lineData.isDstColorFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstColorFactor =
				toBlendFactor(name, fileData.lineIndex);
			lineData.isDstColorFactor = 0;
		}
		else if (lineData.isSrcAlphaFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].srcAlphaFactor =
				toBlendFactor(name, fileData.lineIndex);
			lineData.isSrcAlphaFactor = 0;
		}
		else if (lineData.isDstAlphaFactor)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].dstAlphaFactor =
				toBlendFactor(name, fileData.lineIndex);
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
			blendStates[fileData.blendStateIndex].colorOperation =
				toBlendOperation(name, fileData.lineIndex);
			lineData.isColorOperation = 0;
		}
		else if (lineData.isAlphaOperation)
		{
			if (fileData.blendStateIndex + 1 > blendStates.size())
				blendStates.resize(fileData.blendStateIndex + 1);
			blendStates[fileData.blendStateIndex].alphaOperation =
				toBlendOperation(name, fileData.lineIndex);
			lineData.isAlphaOperation = 0;
		}
		else abort();

		fileData.outputFileStream << lineData.word; fileData.isPipelineState = 2;
	}
}

//--------------------------------------------------------------------------------------------------
static void onShaderFeature(FileData& fileData, LineData& lineData)
{
	if (lineData.word == "bindless")
	{
		fileData.outputFileStream << "#extension "
			"GL_EXT_nonuniform_qualifier : require";
	}
	else
	{
		throw CompileError("unknown GSL feature",
			fileData.lineIndex, lineData.word);
	}
	lineData.isFeature = 0;
}

//--------------------------------------------------------------------------------------------------
static void onShaderVariantCount(FileData& fileData,
	LineData& lineData, uint8& variantCount)
{
	auto count = strtol(lineData.word.c_str(), nullptr, 10);
	if (count <= 1 || count > UINT8_MAX)
	{
		throw CompileError("invalid variant count",
			fileData.lineIndex, lineData.word);
	}
	variantCount = (uint8)count;
	fileData.outputFileStream << "layout(constant_id = 0) const uint gsl_variant = 0; ";
	lineData.isVariantCount = 0;
}

//--------------------------------------------------------------------------------------------------
static void onShaderAttachmentOffset(
	GraphicsFileData& fileData, GraphicsLineData& lineData)
{
	auto offset = strtol(lineData.word.c_str(), nullptr, 10);
	if (offset < 0 || offset > UINT8_MAX)
	{
		throw CompileError("invalid subpass input offset",
			fileData.lineIndex, lineData.word);
	}
	fileData.attachmentIndex += (uint8)offset;
	fileData.outputFileStream << "// #attachmentOffset ";
	lineData.isAttachmentOffset = 0;
}

//--------------------------------------------------------------------------------------------------
static void onShaderGlobalVariable(string& word)
{
	if (word.length() > 3)
	{
		auto index = word.find("vs.");
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + 2] = '_';
			return;
		}
		index = word.find("fs.");
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + 2] = '_';
			return;
		}
		index = word.find("fb.");
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + 2] = '_';
			return;
		}
		index = word.find("gl.");
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + 2] = '_';
			word[index + 3] = toupper(word[index + 3]);
			return;
		}
	}
	if (word.length() > 4)
	{
		auto index = word.find("gsl.");
		if (index != string::npos && (index == 0 || (index > 0 &&
			!isalpha(word[index - 1]) && !isalnum(word[index - 1]))))
		{
			word[index + 3] = '_';
			return;
		}
	}
}

//--------------------------------------------------------------------------------------------------
static bool openShaderFileStream(
	const fs::path& inputFilePath, const fs::path& outputFilePath,
	ifstream& inputFileStream, ofstream& outputFileStream)
{
	inputFileStream = ifstream(inputFilePath);
	if (!inputFileStream.is_open()) return false;
	auto directory = outputFilePath.parent_path();
	if (!fs::exists(directory)) fs::create_directories(directory);
	outputFileStream = ofstream(outputFilePath);
	if (!outputFileStream.is_open())
		throw CompileError("failed to open output shader file");
	outputFileStream.exceptions(ios::failbit | ios::badbit);

	// TODO: half2, double2?
	outputFileStream << "#version 450\n"
		"#define int32 int\n#define uint32 uint\n"
		"#define float2 vec2\n#define float3 vec3\n#define float4 vec4\n"
		"#define int2 ivec2\n#define int3 ivec3\n#define int4 ivec4\n"
		"#define uint2 uvec2\n#define uint3 uvec3\n#define uint4 uvec4\n"
		"#define bool2 bvec2\n#define bool3 bvec3\n#define bool4 bvec4\n"
		"#define float2x2 mat2\n#define float3x3 mat3\n#define float4x4 mat4\n"
		"#define float2x3 mat2x3\n#define float3x2 mat3x2\n"
		"#define float2x4 mat2x4\n#define float4x2 mat4x2\n"
		"#define float3x4 mat3x4\n#define float4x3 mat4x3\n"
		"#line 1\n";
	return true;
}
static void compileShaderFile(const fs::path& filePath,
	const vector<fs::path>& includePaths)
{
	auto command = "glslc --target-env=" GARDEN_VULKAN_SHADER_VERSION_STRING 
		" -c -O \"" + filePath.generic_string() +
		"\" -o \"" + filePath.generic_string() + ".spv\"";
	for (psize i = 0; i < includePaths.size(); i++)
		command += " -I \"" + includePaths[i].generic_string() + "\"";
	auto result = std::system(command.c_str());
	if (result != 0) throw runtime_error("_GLSLC");
	// On some systems file can be still locked. 

	auto attemptCount = 0; 
	while (attemptCount < 10)
	{
		error_code errorCode;
		fs::remove(filePath, errorCode);
		if (errorCode) this_thread::sleep_for(chrono::milliseconds(1));
		else break;
	}
}

//--------------------------------------------------------------------------------------------------
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
static void writeGslHeaderArrays(ofstream& headerStream,
	const map<string, Pipeline::Uniform>& uniforms,
	const map<string, Pipeline::SamplerState>& samplerStates)
{
	for (auto& pair : uniforms)
	{
		GARDEN_ASSERT(pair.first.length() <= UINT8_MAX);
		auto length = (uint8)pair.first.length();
		headerStream.write((char*)&length, sizeof(uint8));
		headerStream.write(pair.first.c_str(), length);
		headerStream.write((const char*)&pair.second, sizeof(Pipeline::Uniform));
	}
	for (auto& pair : samplerStates)
	{
		GARDEN_ASSERT(pair.first.length() <= UINT8_MAX);
		auto length = (uint8)pair.first.length();
		headerStream.write((char*)&length, sizeof(uint8));
		headerStream.write(pair.first.c_str(), length);
		headerStream.write((const char*)&pair.second, sizeof(Pipeline::SamplerState));
	}
}

//--------------------------------------------------------------------------------------------------
static bool compileVertexShader(const fs::path& inputPath, const fs::path& outputPath,
	const vector<fs::path>& includePaths, Compiler::GraphicsData& data,
	uint8& bindingIndex, int8& outIndex, uint16& pushConstantsSize, uint8& variantCount)
{
	const auto shaderStage = ShaderStage::Vertex;
	auto filePath = data.path; filePath += ".vert";
	auto inputFilePath = inputPath / filePath;
	auto outputFilePath = outputPath / filePath;
	GraphicsFileData fileData;
	
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
				else if (lineData.word[0] == '/' && lineData.word[1] == '*')
				{ fileData.isSkipMode = true; }
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos)
					fileData.isSkipMode = false;
				continue;
				// TODO: offset word and process it.
				// Also incorrect line count if uniform sampler2D sampler; // Has some comment
			}
			
			auto overrideOutput = false;

			if (lineData.isIn)
			{
				if (lineData.isIn == 1)
				{
					try { lineData.dataType = toGslDataType(lineData.word); }
					catch (const exception&)
					{
						throw CompileError("unrecognized vertex attribute "
							"GSL data type", fileData.lineIndex, lineData.word);
					}
					fileData.outputFileStream << "layout(location = " <<
						to_string(fileData.inIndex) << ") in ";
					fileData.inIndex += toLocationOffset(lineData.dataType);
					lineData.isIn = 2;
				}
				else if (lineData.isIn == 2)
				{
					onShaderGlobalVariable(lineData.word);
					fileData.outputFileStream << lineData.word;
					overrideOutput = true; lineData.isIn = 3;
				}
				else if (lineData.isIn == 3)
				{
					if (lineData.word == ":")
					{
						overrideOutput = true; lineData.isIn = 4;
					}
					else
					{
						throw CompileError("no ':' after "
							"vertex attribute name", fileData.lineIndex);
					}
				}
				else
				{
					auto name = string(lineData.word, 0,
						lineData.word.find_first_of(';'));
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
						toComponentCount(vertexAttribute.type) *
						toBinarySize(vertexAttribute.format));
					fileData.outputFileStream << "; // " << toString(format);
					overrideOutput = true; lineData.isIn = 0;
				}
			}
			else if (lineData.isOut)
			{
				if (lineData.word == "flat") {
					lineData.isFlat = true; overrideOutput = true; }
				else if (lineData.word == "noperspective") {
					lineData.isNoperspective = true; overrideOutput = true; }
				else
				{
					try { lineData.dataType = toGslDataType(lineData.word); }
					catch (const exception&)
					{
						throw CompileError("unrecognized out "
							"GSL data type", fileData.lineIndex, lineData.word);
					}
					fileData.outputFileStream << "layout(location = " <<
						to_string(outIndex) << ") out ";
					if (lineData.isFlat)
						fileData.outputFileStream << "flat ";
					if (lineData.isNoperspective)
						fileData.outputFileStream << "noperspective ";
					outIndex += toLocationOffset(lineData.dataType);
					lineData.isOut = 0;
				}
			}
			else if (fileData.isUniform)
			{
				onShaderUniform(fileData, lineData, shaderStage,
					bindingIndex, data.uniforms, data.samplerStates);
				overrideOutput = true;
			}
			else if (fileData.isPushConstants)
			{
				onShaderPushConstants(fileData, lineData, pushConstantsSize);
			}
			else if (fileData.isSamplerState)
			{
				onShaderSamplerState(fileData, lineData);
				overrideOutput = true;
			}
			else if (fileData.isPipelineState)
			{
				onShaderPipelineState(fileData, lineData,
					data.pipelineState, data.blendStates);
				overrideOutput = true;
			}
			else if (lineData.isFeature)
			{
				onShaderFeature(fileData, lineData);
				overrideOutput = true;
			}
			else if (lineData.isVariantCount)
			{
				onShaderVariantCount(fileData, lineData, variantCount);
				overrideOutput = true;
			}
			else if (lineData.isAttachmentOffset)
			{
				onShaderAttachmentOffset(fileData, lineData);
			}
			else if (lineData.isAttributeOffset)
			{
				auto offset = strtol(lineData.word.c_str(), nullptr, 10);
				if (offset < 0 || offset > UINT16_MAX)
				{
					throw CompileError("invalid vertex attribute offset",
						fileData.lineIndex, lineData.word);
				}
				data.vertexAttributesSize += (uint16)offset;
				fileData.outputFileStream << "// #attributeOffset ";
				lineData.isAttributeOffset = 0;
			}
			else
			{
				if (lineData.word == "in") {
					lineData.isIn = 1; overrideOutput = true; }
				else if (lineData.word == "out") {
					lineData.isOut = 1; overrideOutput = true; }
				else if (lineData.word == "uniform") {
					fileData.isUniform = 1; overrideOutput = true; }
				else if (lineData.word == "buffer") {
					fileData.isUniform = fileData.isBuffer = 1; overrideOutput = true; }
				else if (lineData.word == "pipelineState") {
					fileData.isPipelineState = 1; overrideOutput = true; }
				else if (lineData.word == "#feature") {
					lineData.isFeature = 1; overrideOutput = true; }
				else if (lineData.word == "#variantCount") {
					lineData.isVariantCount = 1; overrideOutput = true; }
				else if (lineData.word == "#attachmentOffset") {
					lineData.isAttachmentOffset = 1; overrideOutput = true; }
				else if (lineData.word == "#attributeOffset") {
					lineData.isAttributeOffset = 1; overrideOutput = true; }
				else if (lineData.word == "pushConstants")
					throw CompileError("missing 'uniform' keyword", fileData.lineIndex);
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

	outputFilePath += ".spv";
	if (!tryLoadBinaryFile(outputFilePath, data.vertexCode))
		throw CompileError("failed to open compiled shader");
	return true;
}

//--------------------------------------------------------------------------------------------------
static bool compileFragmentShader(const fs::path& inputPath, const fs::path& outputPath,
	const vector<fs::path>& includePaths, Compiler::GraphicsData& data,
	uint8& bindingIndex, int8& inIndex, uint16& pushConstantsSize, uint8& variantCount)
{
	const auto shaderStage = ShaderStage::Fragment;
	auto filePath = data.path; filePath += ".frag";
	auto inputFilePath = inputPath / filePath;
	auto outputFilePath = outputPath / filePath;
	GraphicsFileData fileData;

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
				else if (lineData.word[0] == '/' && lineData.word[1] == '*')
				{ fileData.isSkipMode = true; }
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos)
					fileData.isSkipMode = false;
				continue;
			}

			auto overrideOutput = false;

			if (lineData.isIn)
			{
				if (lineData.word == "flat") {
					lineData.isFlat = true; overrideOutput = true; }
				else if (lineData.word == "noperspective") {
					lineData.isNoperspective = true; overrideOutput = true; }
				else
				{
					try { lineData.dataType = toGslDataType(lineData.word); }
					catch (const exception&)
					{
						throw CompileError("unrecognized in "
							"GSL data type", fileData.lineIndex, lineData.word);
					}
					fileData.outputFileStream << "layout(location = " <<
						to_string(inIndex) << ") in ";
					if (lineData.isFlat)
						fileData.outputFileStream << "flat ";
					if (lineData.isNoperspective)
						fileData.outputFileStream << "noperspective ";
					inIndex += toLocationOffset(lineData.dataType);
					lineData.isIn = 0;
				}
			}
			else if (lineData.isOut)
			{
				if (lineData.isOut == 1)
				{
					try { lineData.dataType = toGslDataType(lineData.word); }
					catch (const exception&)
					{
						throw CompileError("unrecognized out "
							"GSL data type", fileData.lineIndex, lineData.word);
					}
					fileData.outputFileStream << "layout(location = " <<
						to_string(fileData.outIndex) << ") out ";
					fileData.outIndex += toLocationOffset(lineData.dataType);
					lineData.isOut = 2; 
				}
				else if (lineData.isOut == 2)
				{
					onShaderGlobalVariable(lineData.word); lineData.isOut = 0;
				}
			}
			else if (fileData.isUniform)
			{
				onShaderUniform(fileData, lineData, shaderStage,
					bindingIndex, data.uniforms, data.samplerStates);
				overrideOutput = true;
			}
			else if (fileData.isPushConstants)
			{
				onShaderPushConstants(fileData, lineData, pushConstantsSize);
			}
			else if (fileData.isSamplerState)
			{
				onShaderSamplerState(fileData, lineData);
				overrideOutput = true;
			}
			else if (fileData.isPipelineState)
			{
				onShaderPipelineState(fileData, lineData,
					data.pipelineState, data.blendStates);
				overrideOutput = true;
			}
			else if (lineData.isFeature)
			{
				onShaderFeature(fileData, lineData);
				overrideOutput = true;
			}
			else if (lineData.isVariantCount)
			{
				onShaderVariantCount(fileData, lineData, variantCount);
				overrideOutput = true;
			}
			else if (lineData.isAttachmentOffset)
			{
				onShaderAttachmentOffset(fileData, lineData);
			}
			else
			{
				if (lineData.word == "in") {
					lineData.isIn = 1; overrideOutput = true; }
				else if (lineData.word == "out" && !lineData.isDepthOverride) {
					lineData.isOut = 1; overrideOutput = true; }
				else if (lineData.word == "uniform") {
					fileData.isUniform = 1; overrideOutput = true; }
				else if (lineData.word == "buffer") {
					fileData.isUniform = fileData.isBuffer = 1; overrideOutput = true; }
				else if (lineData.word == "pipelineState") {
					fileData.isPipelineState = 1; overrideOutput = true;  }
				else if (lineData.word == "#feature") {
					lineData.isFeature = 1; overrideOutput = true; }
				else if (lineData.word == "#variantCount") {
					lineData.isVariantCount = 1; overrideOutput = true; }
				else if (lineData.word == "#attachmentOffset") {
					lineData.isAttachmentOffset = 1; overrideOutput = true; }
				else if (lineData.word == "depthLess")
				{
					fileData.outputFileStream << "layout(depth_less) ";
					lineData.isDepthOverride = 1; overrideOutput = true;
				}
				else if (lineData.word == "depthGreater")
				{
					fileData.outputFileStream << "layout(depth_greater) ";
					lineData.isDepthOverride = 1; overrideOutput = true;
				}
				else if (lineData.word == "pushConstants")
					throw CompileError("missing 'uniform' keyword", fileData.lineIndex);
				else onShaderGlobalVariable(lineData.word);
			}

			if (!overrideOutput) fileData.outputFileStream << lineData.word << " ";
			lineData.isNewLine = false;
		}

		if (outPosition != fileData.outputFileStream.tellp())
			fileData.outputFileStream << "\n";
	}

	if (data.blendStates.size() < fileData.outIndex)
		data.blendStates.resize(fileData.outIndex);

	fileData.outputFileStream.close();
	compileShaderFile(outputFilePath, includePaths);

	outputFilePath += ".spv";
	if (!tryLoadBinaryFile(outputFilePath, data.fragmentCode))
		throw CompileError("failed to open compiled shader");
	return true;
}

//--------------------------------------------------------------------------------------------------
bool Compiler::compileGraphicsShaders(const fs::path& inputPath,
	const fs::path& outputPath, const vector<fs::path>& includePaths, GraphicsData& data)
{
	GARDEN_ASSERT(!data.path.empty());
	uint16 vertexPushConstantsSize = 0, fragmentPushConstantsSize = 0;
	uint8 vertexVariantCount = 1, fragmentVariantCount = 1;
	uint8 bindingIndex = 0; int8 outIndex = 0, inIndex = 0;

	fs::create_directories(outputPath);
	auto compileResult = compileVertexShader(inputPath, outputPath, includePaths,
		data, bindingIndex, outIndex, vertexPushConstantsSize, vertexVariantCount);
	compileResult |= compileFragmentShader(inputPath, outputPath, includePaths,
		data, bindingIndex, inIndex, fragmentPushConstantsSize, fragmentVariantCount);
	if (!compileResult) return false;

	if (!data.fragmentCode.empty() && !data.vertexCode.empty())
	{
		if (outIndex != inIndex)
			throw CompileError("different vertex and fragment shader in/out count");
	}
	if (vertexVariantCount > 1 && fragmentVariantCount > 1)
	{
		if (vertexVariantCount != fragmentVariantCount)
			throw CompileError("different vertex and fragment variant count");
	}
	if (vertexPushConstantsSize > 0 && fragmentPushConstantsSize > 0)
	{
		if (vertexPushConstantsSize != fragmentPushConstantsSize)
			throw CompileError("different vertex and fragment push constants size");
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

	if (vertexPushConstantsSize > 0)
		data.pushConstantsSize = vertexPushConstantsSize;
	else if (fragmentPushConstantsSize > 0)
		data.pushConstantsSize = fragmentPushConstantsSize;

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
	values.vertexAttributesSize = data.vertexAttributesSize;
	values.vertexAttributeCount = (uint8)data.vertexAttributes.size();
	values.blendStateCount = (uint8)data.blendStates.size();
	values.pushConstantsStages = data.pushConstantsStages;
	values.pipelineState = data.pipelineState;
	
	ofstream headerStream;
	auto headerFilePath = outputPath / data.path; headerFilePath += ".gslh";
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
	
	writeGslHeaderArrays(headerStream, data.uniforms, data.samplerStates);
	return true;
}

//--------------------------------------------------------------------------------------------------
bool Compiler::compileComputeShader(const fs::path& inputPath,
	const fs::path& outputPath, const vector<fs::path>& includePaths, ComputeData& data)
{
	GARDEN_ASSERT(!data.path.empty());
	const auto shaderStage = ShaderStage::Compute;
	auto filePath = data.path; filePath += ".comp";
	auto inputFilePath = inputPath / filePath;
	auto outputFilePath = outputPath / filePath;
	ComputeFileData fileData; uint8 bindingIndex = 0;

	fs::create_directories(outputPath);
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
				else if (lineData.word[0] == '/' && lineData.word[1] == '*')
				{ fileData.isSkipMode = true; }
			}

			if (fileData.isSkipMode)
			{
				fileData.outputFileStream << lineData.word << " ";
				if (lineData.word.find("*/") != string::npos)
					fileData.isSkipMode = false;
				continue;
				// TODO: offset word and process it.
				// Also incorrect line count if uniform sampler2D sampler; // Has some comment
			}

			auto overrideOutput = false;

			if (lineData.isLocalSize)
			{
				if (lineData.isLocalSize == 1)
				{
					if (lineData.word != "=")
					{
						throw CompileError("no '=' after "
							"localSize declaration", fileData.lineIndex);
					}
					lineData.isLocalSize = 2;
				}
				else if (lineData.isLocalSize == 4)
				{
					if (lineData.word.find_first_of(';') == string::npos)
						throw CompileError("no ';' after local size", fileData.lineIndex);
					data.localSize.z = strtol(lineData.word.c_str(), nullptr, 10);
					if (data.localSize.z <= 0)
					{
						throw CompileError("local size 'z' can not be "
							"less than one", fileData.lineIndex);
					}
					fileData.outputFileStream << "layout(local_size_x = " <<
						data.localSize.x << ", local_size_y = " << data.localSize.y <<
						", local_size_z = " << data.localSize.z << ") in; ";
					lineData.isLocalSize = 0;
				}
				else
				{
					if (lineData.word.find_first_of(',') == string::npos)
					{
						throw CompileError("no ',' after "
							"local size dimension", fileData.lineIndex);
					}
					if (lineData.isLocalSize == 2)
					{
						data.localSize.x = strtol(lineData.word.c_str(), nullptr, 10);
						if (data.localSize.x <= 0)
						{
							throw CompileError("local size 'x' can not be "
								"less than one", fileData.lineIndex);
						}
						lineData.isLocalSize = 3;
					}
					else if (lineData.isLocalSize == 3)
					{
						data.localSize.y = strtol(lineData.word.c_str(), nullptr, 10);
						if (data.localSize.y <= 0)
						{
							throw CompileError("local size 'y' can not be "
								"less than one", fileData.lineIndex);
						}
						lineData.isLocalSize = 4;
					}
				}
				overrideOutput = true;
			}
			else if (fileData.isUniform)
			{
				onShaderUniform(fileData, lineData, shaderStage,
					bindingIndex, data.uniforms, data.samplerStates);
				overrideOutput = true;
			}
			else if (fileData.isPushConstants)
			{
				onShaderPushConstants(fileData, lineData, data.pushConstantsSize);
			}
			else if (fileData.isSamplerState)
			{
				onShaderSamplerState(fileData, lineData);
				overrideOutput = true;
			}
			else if (lineData.isFeature)
			{
				onShaderFeature(fileData, lineData);
				overrideOutput = true;
			}
			else if (lineData.isVariantCount)
			{
				onShaderVariantCount(fileData, lineData, data.variantCount);
				overrideOutput = true;
			}
			else
			{
				if (lineData.word == "localSize") {
					lineData.isLocalSize = 1; overrideOutput = true; }
				else if (lineData.word == "uniform") {
					fileData.isUniform = 1; overrideOutput = true; }
				else if (lineData.word == "buffer") {
					fileData.isUniform = fileData.isBuffer = 1; overrideOutput = true; }
				else if (lineData.word == "#feature") {
					lineData.isFeature = 1; overrideOutput = true; }
				else if (lineData.word == "#variantCount") {
					lineData.isVariantCount = 1; overrideOutput = true; }
				else if (lineData.word == "pushConstants")
					throw CompileError("missing 'uniform' keyword", fileData.lineIndex);
				else onShaderGlobalVariable(lineData.word);
			}

			if (!overrideOutput) fileData.outputFileStream << lineData.word << " ";
			lineData.isNewLine = false;
		}

		if (outPosition != fileData.outputFileStream.tellp())
			fileData.outputFileStream << "\n";
	}

	if (data.localSize == 0)
		throw CompileError("undeclared work group localSize");

	GARDEN_ASSERT(data.uniforms.size() <= UINT8_MAX);
	GARDEN_ASSERT(data.samplerStates.size() <= UINT8_MAX);

	fileData.outputFileStream.close();
	compileShaderFile(outputFilePath, includePaths);

	if (data.pushConstantsSize > 0)
		data.pushConstantsStages = ShaderStage::Compute;
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
	values.localSize = data.localSize;
	
	ofstream headerStream;
	auto headerFilePath = outputPath / data.path; headerFilePath += ".gslh";
	writeGslHeaderValues(headerFilePath, computeGslMagic, headerStream, values);
	writeGslHeaderArrays(headerStream, data.uniforms, data.samplerStates);
	headerStream.close();

	outputFilePath += ".spv";
	if (!tryLoadBinaryFile(outputFilePath, data.code))
		throw CompileError("failed to open compiled shader");
	return true;
}
#endif

//--------------------------------------------------------------------------------------------------
template<typename T>
static void readGslHeaderValues(const uint8* data, uint32 dataSize,
	uint32& dataOffset, string_view gslMagic, T& values)
{
	if (dataOffset + GSL_MAGIC_SIZE + sizeof(gslHeader) > dataSize)
		throw runtime_error("Invalid GSL header size.");
	if (memcmp(data + dataOffset, gslMagic.data(), GSL_MAGIC_SIZE) != 0)
		throw runtime_error("Invalid GSL header magic value.");
	dataOffset += GSL_MAGIC_SIZE;
	if (memcmp(data + dataOffset, gslHeader, sizeof(gslHeader)) != 0)
		throw runtime_error("Invalid GSL header version or endianness.");
	dataOffset += sizeof(gslHeader);
	if (dataOffset + sizeof(T) > dataSize)
		throw runtime_error("Invalid GSL header data size.");
	values = *(const T*)(data + dataOffset);
	dataOffset += sizeof(T);
}
static void readGslHeaderArrays(
	const uint8* data, uint32 dataSize, uint32& dataOffset, 
	uint8 uniformCount, uint8 samplerStateCount,
	map<string, Pipeline::Uniform>& uniforms,
	map<string, Pipeline::SamplerState>& samplerStates)
{
	for (uint8 i = 0; i < uniformCount; i++)
	{
		if (dataOffset + sizeof(uint8) > dataSize)
			throw runtime_error("Invalid GSL header data size.");
		auto nameLength = *(const uint8*)(data + dataOffset);
		dataOffset += sizeof(uint8);
		if (dataOffset + nameLength + sizeof(Pipeline::Uniform) > dataSize)
			throw runtime_error("Invalid GSL header data size.");
		string name(nameLength, ' ');
		memcpy(name.data(), data + dataOffset, nameLength);
		dataOffset += nameLength;
		auto& uniform = *(const Pipeline::Uniform*)(data + dataOffset);
		dataOffset += sizeof(Pipeline::Uniform);
		auto result = uniforms.emplace(name, uniform);
		if (!result.second) throw runtime_error("Invalid GSL header data.");
	}
	for (uint8 i = 0; i < samplerStateCount; i++)
	{
		if (dataOffset + sizeof(uint8) > dataSize)
			throw runtime_error("Invalid GSL header data size.");
		auto nameLength = *(const uint8*)(data + dataOffset);
		dataOffset += sizeof(uint8);
		if (dataOffset + nameLength + sizeof(Pipeline::SamplerState) > dataSize)
			throw runtime_error("Invalid GSL header data size.");
		string name(nameLength, ' ');
		memcpy(name.data(), data + dataOffset, nameLength);
		dataOffset += nameLength;
		auto& samplerState = *(const Pipeline::SamplerState*)(data + dataOffset);
		dataOffset += sizeof(Pipeline::SamplerState);
		auto result = samplerStates.emplace(name, samplerState);
		if (!result.second) throw runtime_error("Invalid GSL header data.");
	}
}

//--------------------------------------------------------------------------------------------------
void Compiler::loadGraphicsShaders(GraphicsData& data)
{
	GARDEN_ASSERT(!data.path.empty());
	auto vertexFilePath = "shaders" / data.path; vertexFilePath += ".vert.spv";
	auto fragmentFilePath = "shaders" / data.path; fragmentFilePath += ".frag.spv";
	auto headerFilePath = "shaders" / data.path; headerFilePath += ".gslh";
	vector<uint8> dataBuffer;
	
	#if GARDEN_DEBUG
	if (!tryLoadBinaryFile(GARDEN_CACHES_PATH / headerFilePath, dataBuffer))
		throw runtime_error("Failed to open header file");
	#elif !defined(GSL_COMPILER)
	auto threadIndex = data.threadIndex < 0 ? 0 : data.threadIndex + 1;
	data.packReader->readItemData(headerFilePath, dataBuffer, threadIndex);
	#endif

	GraphicsGslValues values; uint32 dataOffset = 0;
	auto shaderData = dataBuffer.data(); auto dataSize = (uint32)dataBuffer.size();
	readGslHeaderValues(shaderData, dataSize, dataOffset, graphicsGslMagic, values);

	if (dataOffset + values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute) +
		values.blendStateCount * sizeof(GraphicsPipeline::BlendState) > dataSize)
	{
		throw runtime_error("Invalid GSL header data size.");
	}

	if (values.vertexAttributeCount > 0)
	{
		data.vertexAttributes.resize(values.vertexAttributeCount);
		memcpy(data.vertexAttributes.data(), shaderData + dataOffset,
			values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute));
		dataOffset += values.vertexAttributeCount * sizeof(GraphicsPipeline::VertexAttribute);
	}
	if (values.blendStateCount > 0)
	{
		data.blendStates.resize(values.blendStateCount);
		memcpy(data.blendStates.data(), shaderData + dataOffset,
			values.blendStateCount * sizeof(GraphicsPipeline::BlendState));
		dataOffset += values.blendStateCount * sizeof(GraphicsPipeline::BlendState);
	}

	readGslHeaderArrays(shaderData, dataSize, dataOffset,
		values.uniformCount, values.samplerStateCount,
		data.uniforms, data.samplerStates);

	data.pushConstantsSize = values.pushConstantsSize;
	data.descriptorSetCount = values.descriptorSetCount;
	data.variantCount = values.variantCount;
	data.pushConstantsStages = values.pushConstantsStages;
	data.pipelineState = values.pipelineState;
	data.vertexAttributesSize = values.vertexAttributesSize;

	#if GARDEN_DEBUG
	loadBinaryFile(GARDEN_CACHES_PATH / vertexFilePath, data.vertexCode);
	loadBinaryFile(GARDEN_CACHES_PATH / fragmentFilePath, data.fragmentCode);
	#elif !defined(GSL_COMPILER)
	uint64 itemIndex = 0;
	if (data.packReader->getItemIndex(vertexFilePath, itemIndex))
		data.packReader->readItemData(itemIndex, data.vertexCode, threadIndex);
	if (data.packReader->getItemIndex(fragmentFilePath, itemIndex))
		data.packReader->readItemData(itemIndex, data.fragmentCode, threadIndex);
	#endif

	if (data.vertexCode.empty() && data.fragmentCode.empty())
		throw runtime_error("Failed to open graphics shader files");
}

//--------------------------------------------------------------------------------------------------
void Compiler::loadComputeShader(ComputeData& data)
{
	GARDEN_ASSERT(!data.path.empty());
	auto computeFilePath = "shaders" / data.path; computeFilePath += ".comp.spv";
	auto headerFilePath = "shaders" / data.path; headerFilePath += ".gslh";
	vector<uint8> dataBuffer;

	#if GARDEN_DEBUG
	if (!tryLoadBinaryFile(GARDEN_CACHES_PATH / headerFilePath, dataBuffer))
		throw runtime_error("Failed to open header file");
	#elif !defined(GSL_COMPILER)
	auto threadIndex = data.threadIndex < 0 ? 0 : data.threadIndex + 1;
	data.packReader->readItemData(headerFilePath, dataBuffer, threadIndex);
	#endif

	ComputeGslValues values; uint32 dataOffset = 0; 
	auto shaderData = dataBuffer.data(); auto dataSize = (uint32)dataBuffer.size();
	readGslHeaderValues(shaderData, dataSize, dataOffset, computeGslMagic, values);

	readGslHeaderArrays(shaderData, dataSize, dataOffset,
		values.uniformCount, values.samplerStateCount,
		data.uniforms, data.samplerStates);

	data.pushConstantsSize = values.pushConstantsSize;
	data.descriptorSetCount = values.descriptorSetCount;
	data.variantCount = values.variantCount;
	data.localSize = values.localSize;

	if (data.pushConstantsSize > 0)
		data.pushConstantsStages = ShaderStage::Compute;

	#if GARDEN_DEBUG
	if (!tryLoadBinaryFile(GARDEN_CACHES_PATH / computeFilePath, data.code))
		throw runtime_error("Failed to open compute shader file");
	#elif !defined(GSL_COMPILER)
	data.packReader->readItemData(computeFilePath, data.code, threadIndex);
	#endif
}

#ifdef GSL_COMPILER
//--------------------------------------------------------------------------------------------------
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
	
	for (int i = 1; i < argc; i++)
	{
		auto arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
		{
			cout << "(C) 2022-2023 Nikita Fediuchin. All rights reserved.\n"
				"gslc - Garden Shader Language Compiler (GLSL dialect)\n"
				"\n"
				"Usage: gslc [options] name...\n"
				"\n"
				"Options:\n"
				"  -i <dir>		Read input from <dir>.\n"
				"  -o <dir>		Write output to <dir>.\n"
				"  -I <value>   Add directory to include search path.\n"
				"  -h			Display available options.\n"
				"  --help		Display available options.\n"
				"  --version	Display compiler version information.\n";
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

			inputPath = argv[i++ + 1];
			logOffset += 2;
		}
		else if (strcmp(arg, "-o") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no output directory\n";
				return EXIT_FAILURE;
			}

			outputPath = argv[i++ + 1];
			logOffset += 2;
		}
		else if (strcmp(arg, "-I") == 0)
		{
			if (i + 1 >= argc)
			{
				cout << "gslc: error: no include directory\n";
				return EXIT_FAILURE;
			}

			includePaths.push_back(argv[i++ + 1]);
			logOffset += 2;
		}
		else if (arg[0] == '-')
		{
			cout << "gslc: error: unsupported option: '" << arg << "'\n";
			return EXIT_FAILURE;
		}
		else
		{
			// TODO: do this from multiple thread.

			int progress = (int)(((float)((i - logOffset) + 1) /
				(float)(argc - logOffset)) * 100.0f);
			const char* spacing;
			if (progress < 10) spacing = "  ";
			else if (progress < 100) spacing = " ";
			else spacing = "";

			cout << "[" << spacing << progress << "%] " <<
				"Compiling shader " << arg << "\n" << flush;

			auto compileResult = false;
			try
			{
				Compiler::GraphicsData graphicsData;
				graphicsData.path = arg;

				compileResult |= Compiler::compileGraphicsShaders(
					inputPath, outputPath, includePaths, graphicsData);
			}
			catch (const exception& e)
			{
				if (strcmp(e.what(), "_GLSLC") != 0)
					cout << e.what() << '\n';
			}
	
			try
			{
				Compiler::ComputeData computeData;
				computeData.path = arg;

				compileResult |= Compiler::compileComputeShader(
					inputPath, outputPath, includePaths, computeData);
			}
			catch (const exception& e)
			{
				if (strcmp(e.what(), "_GLSLC") != 0)
					cout << e.what() << '\n';
			}

			if (!compileResult)
			{
				cout << "gslc: error: no shader files found (" << arg << ")\n";
				return EXIT_FAILURE;
			}
		}
	}

	return EXIT_SUCCESS;
}
#endif
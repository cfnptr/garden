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
#include "math/matrix.hpp"
#include "garden/hash.hpp"
#include "garden/graphics/image.hpp"
#include "garden/graphics/descriptor-set.hpp"

#include <thread>

namespace garden::graphics
{

using namespace std;
using namespace math;
using namespace ecsm;
class PipelineExt;

//--------------------------------------------------------------------------------------------------
class Pipeline : public Resource
{
public:
	enum class SamplerWrap : uint8
	{
		Repeat, MirroredRepeat, ClampToEdge, ClampToBorder, MirrorClampToEdge, Count
	};
	enum class BorderColor : uint8
	{
		FloatTransparentBlack, IntTransparentBlack,
		FloatOpaqueBlack, IntOpaqueBlack,
		FloatOpaqueWhite, IntOpaqueWhite, Count
	};
	enum class CompareOperation : uint8
	{
		Never, Less, Equal, LessOrEqual, Greater,
		NotEqual, GreaterOrEqual, Always, Count
	};

	struct Uniform final
	{
		GslUniformType type = {};
		ShaderStage shaderStages = {};
		uint8 bindingIndex = 0;
		uint8 descriptorSetIndex = 0;
		uint8 arraySize = 0;
		bool readAccess = true;
		bool writeAccess = true;
		uint8 _alignment = 0; // should be algined.
	};
	struct DescriptorData final
	{
		ID<DescriptorSet> set;
		uint32 count = 0;
		uint32 offset = 0;

		DescriptorData(ID<DescriptorSet> set,
			uint32 count = 1, uint32 offset = 0) noexcept
		{
			this->set = set; this->count = count; this->offset = offset;
		}
		DescriptorData() = default;
	};
	struct SamplerState final
	{
		uint8 anisoFiltering : 1;
		uint8 comparing : 1;
		uint8 unnormCoords : 1;
		uint8 _unused : 5;
		SamplerFilter minFilter = SamplerFilter::Nearest;
		SamplerFilter magFilter = SamplerFilter::Nearest;
		SamplerFilter mipmapFilter = SamplerFilter::Nearest;
		SamplerWrap wrapX = SamplerWrap::ClampToEdge;
		SamplerWrap wrapY = SamplerWrap::ClampToEdge;
		SamplerWrap wrapZ = SamplerWrap::ClampToEdge;
		CompareOperation compareOperation = CompareOperation::Less;
		float maxAnisotropy = 0.0f;
		float mipLodBias = 0.0f;
		float minLod = 0.0f, maxLod = INFINITY;
		BorderColor borderColor = BorderColor::FloatTransparentBlack;
		uint8 _alignment0 = 0; uint16 _alignment1 = 0; // should be algined.
		SamplerState() : anisoFiltering(0), comparing(0), unnormCoords(0), _unused(0) { }
	};
	struct CreateData
	{
		fs::path path;
		map<string, SamplerState> samplerStates;
		map<string, Uniform> uniforms;
		uint64 pipelineVersion = 0;
		uint32 maxBindlessCount = 0;
		uint16 pushConstantsSize = 0;
		uint8 descriptorSetCount = 0;
		uint8 variantCount = 0;
		ShaderStage pushConstantsStages = {};
	};
//--------------------------------------------------------------------------------------------------
protected:
	map<string, Uniform> uniforms;
	vector<uint8> pushConstantsBuffer;
	vector<void*> samplers;
	vector<void*> descriptorSetLayouts;
	vector<void*> descriptorPools;
	fs::path pipelinePath;
	void* pipelineCache = nullptr;
	void* pipelineLayout = nullptr; 
	uint64 pipelineVersion = 0;
	uint32 maxBindlessCount = 0;
	uint32 pushConstantsMask = 0;
	uint16 pushConstantsSize = 0;
	PipelineType type = {};
	uint8 variantCount = 0;
	bool useAsync = false;
	bool cacheLoaded = false;
	bool bindless = false;

	Pipeline() = default;
	Pipeline(CreateData& createData, bool useAsync);
	Pipeline(PipelineType type, const fs::path& path,
		uint32 maxBindlessCount, bool useAsync, uint64 pipelineVersion)
	{
		this->pipelinePath = path;
		this->pipelineVersion = pipelineVersion;
		this->maxBindlessCount = maxBindlessCount;
		this->type = type;
		this->useAsync = useAsync;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (type == PipelineType::Graphics)
			debugName = "graphicsPipeline." + path.generic_string();
		else if (type == PipelineType::Compute)
			debugName = "computePipeline." + path.generic_string();
		else abort();
		#endif
	}
	bool destroy() final;

	static vector<void*> createShaders(
		const vector<vector<uint8>>& code, const fs::path& path);
	static void destroyShaders(const vector<void*>& shaders);

	static void updateDescriptorTime(
		const DescriptorData* descriptorData, uint8 descriptorDataSize);

	friend class PipelineExt;
	friend class DescriptorSet;
//--------------------------------------------------------------------------------------------------
public:
	PipelineType getType() const noexcept { return type; }
	const fs::path& getPath() const noexcept { return pipelinePath; }
	const map<string, Uniform>& getUniforms() const noexcept { return uniforms; }
	const vector<uint8>& getPushConstantsBuffer()
		const noexcept { return pushConstantsBuffer; }
	uint16 getPushConstantsSize() const noexcept { return pushConstantsSize; }
	uint32 getMaxBindlessCount() const noexcept { return maxBindlessCount; }
	uint8 getVariantCount() const noexcept { return variantCount; }
	bool isUseAsync() const noexcept { return useAsync; }
	bool isBindless() const noexcept { return bindless; }
	bool isReady() const noexcept { return instance; }

	template<typename T>
	T* getPushConstants()
	{
		GARDEN_ASSERT(!useAsync);
		GARDEN_ASSERT(pushConstantsSize == sizeof(T));
		return (T*)pushConstantsBuffer.data();
	}
	template<typename T>
	const T* getPushConstants() const
	{ return getPushConstants<T>(); }
	
	template<typename T>
	T* getPushConstantsAsync(int32 taskIndex)
	{
		GARDEN_ASSERT(useAsync);
		GARDEN_ASSERT(taskIndex >= 0);
		GARDEN_ASSERT(taskIndex < (int32)thread::hardware_concurrency());
		GARDEN_ASSERT(pushConstantsSize == sizeof(T));
		return (T*)(pushConstantsBuffer.data() + pushConstantsSize * taskIndex);
	}
	template<typename T>
	const T* getPushConstantsAsync(int32 taskIndex) const
	{ return getPushConstantsAsync<T>(taskIndex); }

//--------------------------------------------------------------------------------------------------
// Render commands
//--------------------------------------------------------------------------------------------------

	void bind(uint8 variant = 0);
	void bindAsync(uint8 variant = 0, int32 taskIndex = -1);

	void bindDescriptorSets(const DescriptorData* descriptorData,
		uint8 descriptorDataSize);
	void bindDescriptorSetsAsync(const DescriptorData* descriptorData,
		uint8 descriptorDataSize, int32 taskIndex = -1);

	template<psize N>
	void bindDescriptorSets(const array<DescriptorData, N>& descriptorData)
	{ bindDescriptorSets(descriptorData.data(), (uint8)N); }
	void bindDescriptorSets(const vector<DescriptorData>& descriptorData)
	{ bindDescriptorSets(descriptorData.data(), (uint8)descriptorData.size()); }
	void bindDescriptorSet(ID<DescriptorSet> descriptorSet, uint32 offset = 0)
	{
		DescriptorData descriptorData(descriptorSet, 1, offset);
		bindDescriptorSets(&descriptorData, 1);
	}
	
	template<psize N>
	void bindDescriptorSetsAsync(const array<DescriptorData, N>& descriptorData,
		int32 taskIndex = -1)
	{ bindDescriptorSetsAsync(descriptorData.data(), (uint8)N, taskIndex); }
	void bindDescriptorSetsAsync(const vector<DescriptorData>& descriptorData,
		int32 taskIndex = -1)
	{
		bindDescriptorSetsAsync(descriptorData.data(),
			(uint8)descriptorData.size(), taskIndex);
	}
	void bindDescriptorSetAsync(ID<DescriptorSet> descriptorSet,
		uint32 offset = 0, int32 taskIndex = -1)
	{
		DescriptorData descriptorData(descriptorSet, 1, offset);
		bindDescriptorSetsAsync(&descriptorData, 1, taskIndex);
	}
	
	void pushConstants();
	void pushConstantsAsync(int32 taskIndex);
};

//--------------------------------------------------------------------------------------------------
static Pipeline::SamplerWrap toSamplerWrap(string_view samplerWrap)
{
	if (samplerWrap == "repeat") return Pipeline::SamplerWrap::Repeat;
	if (samplerWrap == "mirroredRepeat") return Pipeline::SamplerWrap::MirroredRepeat;
	if (samplerWrap == "clampToEdge") return Pipeline::SamplerWrap::ClampToEdge;
	if (samplerWrap == "clampToBorder") return Pipeline::SamplerWrap::ClampToBorder;
	if (samplerWrap == "mirrorClampToEdge") return Pipeline::SamplerWrap::MirrorClampToEdge;
	throw runtime_error("Unknown sampler wrap type. (" + string(samplerWrap) + ")");
}
static Pipeline::BorderColor toBorderColor(string_view borderColor)
{
	if (borderColor == "floatTransparentBlack")
		return Pipeline::BorderColor::FloatTransparentBlack;
	if (borderColor == "intTransparentBlack")
		return Pipeline::BorderColor::IntTransparentBlack;
	if (borderColor == "floatOpaqueBlack")
		return Pipeline::BorderColor::FloatOpaqueBlack;
	if (borderColor == "intOpaqueBlack")
		return Pipeline::BorderColor::IntOpaqueBlack;
	if (borderColor == "floatOpaqueWhite")
		return Pipeline::BorderColor::FloatOpaqueWhite;
	if (borderColor == "intOpaqueWhite")
		return Pipeline::BorderColor::IntOpaqueWhite;
	throw runtime_error("Unknown border color type. (" + string(borderColor) + ")");
}
static Pipeline::CompareOperation toCompareOperation(string_view compareOperation)
{
	if (compareOperation == "never") return Pipeline::CompareOperation::Never;
	if (compareOperation == "less") return Pipeline::CompareOperation::Less;
	if (compareOperation == "equal") return Pipeline::CompareOperation::Equal;
	if (compareOperation == "lessOrEqual") return Pipeline::CompareOperation::LessOrEqual;
	if (compareOperation == "greater") return Pipeline::CompareOperation::Greater;
	if (compareOperation == "notEqual") return Pipeline::CompareOperation::NotEqual;
	if (compareOperation == "greaterOrEqual") return Pipeline::CompareOperation::GreaterOrEqual;
	if (compareOperation == "always") return Pipeline::CompareOperation::Always;
	throw runtime_error("Unknown compare operation type. (" +
		string(compareOperation) + ")");
}

//--------------------------------------------------------------------------------------------------
static const string_view samplerWrapNames[(psize)Pipeline::SamplerWrap::Count] =
{
	"Repeat", "MirroredRepeat", "ClampToEdge", "ClampToBorder", "MirrorClampToEdge"
};
static const string_view borderColorNames[(psize)Pipeline::BorderColor::Count] =
{
	"FloatTransparentBlack", "IntTransparentBlack", "FloatOpaqueBlack",
	"IntOpaqueBlack", "FloatOpaqueWhite", "IntOpaqueWhite"
};
static const string_view compareOperationNames[(psize)Pipeline::CompareOperation::Count] =
{
	"Never", "Less", "Equal", "LessOrEqual",
	"Greater", "NotEqual", "GreaterOrEqual", "Always"
};

static string_view toString(Pipeline::SamplerWrap samplerWrap)
{
	GARDEN_ASSERT((uint8)samplerWrap < (uint8)Pipeline::SamplerWrap::Count);
	return samplerWrapNames[(psize)samplerWrap];
}
static string_view toString(Pipeline::BorderColor borderColor)
{
	GARDEN_ASSERT((uint8)borderColor < (uint8)Pipeline::BorderColor::Count);
	return borderColorNames[(psize)borderColor];
}
static string_view toString(Pipeline::CompareOperation compareOperation)
{
	GARDEN_ASSERT((uint8)compareOperation < (uint8)Pipeline::CompareOperation::Count);
	return compareOperationNames[(psize)compareOperation];
}

//--------------------------------------------------------------------------------------------------
class PipelineExt final
{
public:
	static map<string, Pipeline::Uniform>& getUniforms(Pipeline& pipeline)
		noexcept { return pipeline.uniforms; }
	static vector<uint8>& getPushConstantsBuffer(Pipeline& pipeline)
		noexcept { return pipeline.pushConstantsBuffer; }
	static vector<void*>& getSamplers(Pipeline& pipeline)
		noexcept { return pipeline.samplers; }
	static vector<void*>& getDescriptorSetLayouts(Pipeline& pipeline)
		noexcept { return pipeline.descriptorSetLayouts; }
	static vector<void*>& getDescriptorPools(Pipeline& pipeline)
		noexcept { return pipeline.descriptorPools; }
	static void*& getCache(Pipeline& pipeline)
		noexcept { return pipeline.pipelineCache; }
	static void*& getLayout(Pipeline& pipeline)
		noexcept { return pipeline.pipelineLayout; }
	static uint64& getVersion(Pipeline& pipeline)
		noexcept { return pipeline.pipelineVersion; }
	static uint32& getPushConstantsMask(Pipeline& pipeline)
		noexcept { return pipeline.pushConstantsMask; }
	static uint16& getPushConstantsSize(Pipeline& pipeline)
		noexcept { return pipeline.pushConstantsSize; }
	static uint8& getVariantCount(Pipeline& pipeline)
		noexcept { return pipeline.variantCount; }
	static bool& isCacheLoaded(Pipeline& pipeline)
		noexcept { return pipeline.cacheLoaded; }
	static bool& isBindless(Pipeline& pipeline)
		noexcept { return pipeline.bindless; }
	
	static void moveInternalObjects(Pipeline& source, Pipeline& destination) noexcept
	{
		PipelineExt::getUniforms(destination) =
			std::move(PipelineExt::getUniforms(source));
		PipelineExt::getPushConstantsBuffer(destination) =
			std::move(PipelineExt::getPushConstantsBuffer(source));
		PipelineExt::getSamplers(destination) =
			std::move(PipelineExt::getSamplers(source));
		PipelineExt::getDescriptorSetLayouts(destination) =
			std::move(PipelineExt::getDescriptorSetLayouts(source));
		PipelineExt::getDescriptorPools(destination) =
			std::move(PipelineExt::getDescriptorPools(source));
		PipelineExt::getCache(destination) = PipelineExt::getCache(source);
		PipelineExt::getLayout(destination) = PipelineExt::getLayout(source);
		PipelineExt::getPushConstantsMask(destination) =
			PipelineExt::getPushConstantsMask(source);
		PipelineExt::getPushConstantsSize(destination) =
			PipelineExt::getPushConstantsSize(source);
		PipelineExt::getVariantCount(destination) = PipelineExt::getVariantCount(source);
		PipelineExt::isCacheLoaded(destination) = PipelineExt::isCacheLoaded(source);
		PipelineExt::isBindless(destination) = PipelineExt::isBindless(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		ResourceExt::getInstance(source) = nullptr;
	}
	static void destroy(Pipeline& pipeline) { pipeline.destroy(); }
};

} // garden::graphics
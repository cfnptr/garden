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
 * @brief Common rendering pipeline functions.
 */

#pragma once
#include "garden/graphics/image.hpp"
#include "garden/graphics/descriptor-set.hpp"
#include "math/matrix.hpp"

#include <thread>

namespace garden::graphics
{

using namespace std;
using namespace math;
using namespace ecsm;
class PipelineExt;

/**
 * @brief Rendering stages container.
 * 
 * @details
 * Pipeline is a fundamental concept representing the entire state of the graphics or compute operations. 
 * It encapsulates all the stages of processing that the data will go through, from input to output.
 */
class Pipeline : public Resource
{
public:
	/*******************************************************************************************************************
	 * @brief Sampler wrap mode. (texture address)
	 * 
	 * @details
	 * Setting that determines how a texture is applied (or "sampled") when texture coordinates (also known as UV 
	 * coordinates) fall outside the standard range of [0, 1]. This scenario frequently occurs due to texture 
	 * mapping or repeating textures across a surface. The wrap mode defines how the GPU handles these 
	 * out-of-range texture coordinates, affecting the appearance of textured objects in 3D scenes.
	 */
	enum class SamplerWrap : uint8
	{
		Repeat,            /**< i = ​i % size */
		MirroredRepeat,    /**< i = (size - 1) - mirror((i % (2 * size)) - size) */
		ClampToEdge,       /**< i = clamp(i, 0, size - 1) */
		ClampToBorder,     /**< i = clamp(i, -1, size) */
		MirrorClampToEdge, /**< i = clamp(mirror(i), 0, size - 1) */
		Count              /**< Sampler wrap mode count. */
	};
	/**
	 * @brief Clamp to border sampling color.
	 * 
	 * @details
	 * Color applied to pixels that fall outside the texture coordinates when using @ref ClampToBorder sampling modes.
	 * 
	 * @todo Support custom color extension?
	 */
	enum class BorderColor : uint8
	{
		FloatTransparentBlack, /**< Transparent, floating point format, black color. */
		IntTransparentBlack,   /**< transparent, integer format, black color. */
		FloatOpaqueBlack,      /**< Opaque, floating point format, black color. */
		IntOpaqueBlack,        /**< Opaque, integer format, black color. */
		FloatOpaqueWhite,      /**< Opaque, floating point format, white color. */
		IntOpaqueWhite,        /**< Opaque, integer format, white color. */
		Count                  /**< Clamp to border sampling color count. */
	};
	/**
	 * @brief Comparison operator for depth, stencil, and sampler operations
	 * 
	 * @details
	 * Used to compare two values against each other. These operators are fundamental in various graphics operations, 
	 * such as depth testing, stencil testing, and shadow mapping, where decisions are made based on comparing 
	 * values like depth (Z) values of fragments or stencil buffer values.
	 */
	enum class CompareOperation : uint8
	{
		Never,          /**< Comparison always evaluates false. */
		Less,           /**< Comparison evaluates reference < test. */
		Equal,          /**< Comparison evaluates reference == test. */
		LessOrEqual,    /**< Comparison evaluates reference <= test. */
		Greater,        /**< Comparison evaluates reference > test. */
		NotEqual,       /**< Comparison evaluates reference != test. */
		GreaterOrEqual, /**< Comparison evaluates reference >= test. */
		Always,         /**< Comparison always evaluates true. */
		Count           /**< Comparison operator type count. */
	};

	/*******************************************************************************************************************
	 * @brief Uniform variable description.
	 * 
	 * @details
	 * Uniform is a type of variable used in shader programs to represent data that remains constant for an entire 
	 * render pass, drawing or compute call, in contrast to other types of shader variables that may change per vertex 
	 * or per fragment. Uniforms are commonly used to pass data from the CPU-side application to the GPU-side shaders 
	 * without having to resend this data for each vertex or fragment processed. This makes uniforms extremely 
	 * efficient for data that applies globally to a scene or a rendering operation, such as transformation 
	 * matrices, material properties, lighting information, and global settings.
	 */
	struct Uniform final
	{
		GslUniformType type = {};      /**< Uniform variable type. */
		ShaderStage shaderStages = {}; /**< Shader stages where uniform is used. */
		uint8 bindingIndex = 0;        /**< Binding index inside the descriptor set. */
		uint8 descriptorSetIndex = 0;  /**< Index of the descriptor set. */
		uint8 arraySize = 0;           /**< Number of descriptors contained in the binding. */
		bool readAccess = true;        /**< Is variable read access allowed. */
		bool writeAccess = true;       /**< Is variable write access allowed. */
		uint8 _alignment = 0;          /**< [structure alignment] */
		// Note: Should be aligned.
	};

	/**
	 * @brief Sampler configuration.
	 * 
	 * @details
	 * Configuration used to determine how a texture is sampled when applied to a 3D model. The process of mapping 
	 * these textures onto the 3D surfaces is called texture mapping, and sampling is a crucial part of this process. 
	 * The sampler state defines how the texture is accessed and filtered when it is being applied to a model.
	 */
	struct SamplerState final
	{
		uint8 anisoFiltering : 1;                                     /**< Is anisotropic filtering enabled. */
		uint8 comparison : 1;                                         /**< Is comparison during lookups enabled. */
		uint8 unnormCoords : 1;                                       /**< Is unnormalized coordinates enabled. */
		uint8 _unused : 5;                                            /**< [reserved for future use] */
		SamplerFilter minFilter = SamplerFilter::Nearest;             /**< Minification filter to apply to lookups. */
		SamplerFilter magFilter = SamplerFilter::Nearest;             /**< Magnification filter to apply to lookups. */
		SamplerFilter mipmapFilter = SamplerFilter::Nearest;          /**< Mipmap filter to apply to lookups. */
		SamplerWrap wrapX = SamplerWrap::ClampToEdge;                 /**< Addressing mode for U coordinates outside [0,1) */
		SamplerWrap wrapY = SamplerWrap::ClampToEdge;                 /**< Addressing mode for V coordinates outside [0,1) */
		SamplerWrap wrapZ = SamplerWrap::ClampToEdge;                 /**< Addressing mode for W coordinates outside [0,1) */
		CompareOperation compareOperation = CompareOperation::Less;   /**< Comparison operator to apply to fetched data/ */
		float maxAnisotropy = 0.0f;                                   /**< Anisotropy value clamp used by the sampler. */
		float mipLodBias = 0.0f;                                      /**< Bias to be added to mipmap LOD calculation. */
		float minLod = 0.0f;                                          /**< Used to clamp the minimum of the computed LOD value. */
		float maxLod = INFINITY;                                      /**< Used to clamp the maximum of the computed LOD value. */
		BorderColor borderColor = BorderColor::FloatTransparentBlack; /**< Predefined border color to use. */
		uint8 _alignment0 = 0;                                        /**< [structure alignment] */
		uint16 _alignment1 = 0;                                       /**< [structure alignment] */
		// should be aligned.

		SamplerState() : anisoFiltering(0), comparison(0), unnormCoords(0), _unused(0) { }
	};

	/*******************************************************************************************************************
	 * @brief Specialization constant variable description.
	 * 
	 * @details
	 * Specialization constants allow for certain values within shaders to be determined at pipeline 
	 * creation time rather than hardcoded at the time of shader compilation. This enables a more 
	 * flexible and efficient use of shaders, as you can customize shader behavior without needing 
	 * to recompile the shader from source for each variation.
	 */
	struct SpecConst
	{
		ShaderStage shaderStages = {};
		GslDataType dataType = {};
		uint8 index = 0;
		uint8 _alignment = 0;
	};

	struct SpecConstBase { GslDataType type = {}; uint32 data[4]; };
	struct SpecConstBool { GslDataType type = {}; uint32 value = false; };
	struct SpecConstInt32 { GslDataType type = {}; int32 value = 0; };
	struct SpecConstUint32 { GslDataType type = {}; uint32 value = 0; };
	struct SpecConstFloat { GslDataType type = {}; float value = 0.0f; };
	struct SpecConstInt2 { GslDataType type = {}; int2 value = int2(0); };
	struct SpecConstUint2 { GslDataType type = {}; uint2 value = uint2(0); };
	struct SpecConstFloat2 { GslDataType type = {}; float2 value = float2(0.0f); };
	struct SpecConstInt3 { GslDataType type = {}; int3 value = int3(0); };
	struct SpecConstUint3 { GslDataType type = {}; uint3 value = uint3(0); };
	struct SpecConstFloat3 { GslDataType type = {}; float3 value = float3(0.0f); };
	struct SpecConstInt4 { GslDataType type = {}; int4 value = int4(0); };
	struct SpecConstUint4 { GslDataType type = {}; uint4 value = uint4(0); };
	struct SpecConstFloat4 { GslDataType type = {}; float4 value = float4(0.0f); };

	/**
	 * @brief Specialization constant variable container.
	 * @details See the @ref Pipeline::SpecConst for more details.
	 */
	union SpecConstValue
	{
		SpecConstBase constBase;     /**< General specialization constant variable. */
		SpecConstBool constBool;     /**< Boolean specialization constant variable. */
		SpecConstInt32 constInt32;   /**< 32-bit signed integer specialization constant variable. */
		SpecConstUint32 constUint32; /**< 32-bit unsigned integer specialization constant variable. */
		SpecConstFloat constFloat;   /**< 32-bit floating point specialization constant variable. */
		SpecConstInt2 constInt2;     /**< 2D 32-bit signed integer specialization constant variable. */
		SpecConstUint2 constUint2;   /**< 2D 32-bit unsigned integer specialization constant variable. */
		SpecConstFloat2 constFloat2; /**< 2D 32-bit floating point specialization constant variable. */
		SpecConstInt3 constInt3;     /**< 3D 32-bit signed integer specialization constant variable. */
		SpecConstUint3 constUint3;   /**< 3D 32-bit unsigned integer specialization constant variable. */
		SpecConstFloat3 constFloat3; /**< 3D 32-bit floating point specialization constant variable. */
		SpecConstInt4 constInt4;     /**< 4D 32-bit signed integer specialization constant variable. */
		SpecConstUint4 constUint4;     /**< 4D 32-bit signed integer specialization constant variable. */
		SpecConstFloat4 constFloat4; /**< 4D 32-bit floating point specialization constant variable. */

		SpecConstValue() { constInt4.value = int4(0); }
		SpecConstValue(bool value) { constBool.value = value; constBool.type = GslDataType::Bool; }
		SpecConstValue(int32 value) { constInt32.value = value; constBool.type = GslDataType::Int32; }
		SpecConstValue(uint32 value) { constUint32.value = value; constBool.type = GslDataType::Uint32; }
		SpecConstValue(float value) { constFloat.value = value; constBool.type = GslDataType::Float; }
		SpecConstValue(int2 value) { constInt2.value = value; constBool.type = GslDataType::Int2; }
		SpecConstValue(uint2 value) { constInt2.value = value; constBool.type = GslDataType::Uint2; }
		SpecConstValue(float2 value) { constFloat2.value = value; constBool.type = GslDataType::Float2; }
		SpecConstValue(const int3& value) { constInt3.value = value; constBool.type = GslDataType::Int3; }
		SpecConstValue(const uint3& value) { constInt3.value = value; constBool.type = GslDataType::Uint3; }
		SpecConstValue(const float3& value) { constFloat3.value = value; constBool.type = GslDataType::Float3; }
		SpecConstValue(const int4& value) { constInt4.value = value; constBool.type = GslDataType::Int4; }
		SpecConstValue(const uint4& value) { constInt4.value = value; constBool.type = GslDataType::Uint4; }
		SpecConstValue(const float4& value) { constFloat4.value = value; constBool.type = GslDataType::Float4; }
	};

	/*******************************************************************************************************************
	 * @brief Rendering pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct CreateData
	{
		fs::path shaderPath;
		map<string, SamplerState> samplerStates;
		map<string, Uniform> uniforms;
		map<string, SpecConst> specConsts;
		map<string, SpecConstValue> specConstValues;
		map<string, SamplerState> samplerStateOverrides;
		uint64 pipelineVersion = 0;
		uint32 maxBindlessCount = 0;
		uint16 pushConstantsSize = 0;
		uint8 descriptorSetCount = 0;
		uint8 variantCount = 0;
		ShaderStage pushConstantsStages = {};
	};
protected:
	map<string, Uniform> uniforms;
	vector<uint8> pushConstantsBuffer;
	vector<void*> samplers;
	vector<void*> descriptorSetLayouts;
	vector<void*> descriptorPools;
	fs::path pipelinePath;
	void* pipelineLayout = nullptr; 
	uint64 pipelineVersion = 0;
	uint32 maxBindlessCount = 0;
	uint32 pushConstantsMask = 0;
	uint16 pushConstantsSize = 0;
	PipelineType type = {};
	uint8 variantCount = 0;
	bool asyncRecording = false;
	bool bindless = false;

	// Note: Use GraphicsSystem to create, destroy and access pipelines.

	Pipeline() = default;
	Pipeline(CreateData& createData, bool useAsyncRecording);
	Pipeline(PipelineType type, const fs::path& path, uint32 maxBindlessCount,
		bool useAsyncRecording, uint64 pipelineVersion)
	{
		this->pipelinePath = path;
		this->pipelineVersion = pipelineVersion;
		this->maxBindlessCount = maxBindlessCount;
		this->type = type;
		this->asyncRecording = useAsyncRecording;

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (type == PipelineType::Graphics)
			debugName = "graphicsPipeline." + path.generic_string();
		else if (type == PipelineType::Compute)
			debugName = "computePipeline." + path.generic_string();
		else abort();
		#endif
	}
	bool destroy() final;

	static vector<void*> createShaders(const vector<vector<uint8>>& code, const fs::path& path);
	static void destroyShaders(const vector<void*>& shaders);

	static void fillSpecConsts(
		const fs::path& path, ShaderStage shaderStage, uint8 variantCount, void* specInfo,
		const map<string, SpecConst>& specConsts, const map<string, SpecConstValue>& specConstValues);
	static void updateDescriptorsLock(const DescriptorSet::Range* descriptorSetRange, uint8 descriptorDataSize);

	friend class PipelineExt;
	friend class DescriptorSet;
public:
	/*******************************************************************************************************************
	 * @brief Returns rendering pipeline type.
	 * @details General Pipeline class contains shared functional between all pipeline types.
	 */
	PipelineType getType() const noexcept { return type; }
	/**
	 * @brief Returns pipeline resource path.
	 * @details Same as what was used to create the pipeline.
	 */
	const fs::path& getPath() const noexcept { return pipelinePath; }
	/**
	 * @brief Returns pipeline uniforms map.
	 * @details Uniforms are loaded from the compiled shader files.
	 */
	const map<string, Uniform>& getUniforms() const noexcept { return uniforms; }
	/**
	 * @brief Returns pipeline push constants data buffer.
	 * @details You can use it to access written push constants data.
	 */
	const vector<uint8>& getPushConstantsBuffer() const noexcept { return pushConstantsBuffer; }
	/**
	 * @brief Returns pipeline push constants buffer size in bytes.
	 * @details Calculated from the shader push constants structure during compilation.
	 */
	uint16 getPushConstantsSize() const noexcept { return pushConstantsSize; }
	/**
	 * @brief Returns pipeline maximum bindless descriptor count in the array.
	 * @details Used to preallocate required space in the descriptor set.
	 */
	uint32 getMaxBindlessCount() const noexcept { return maxBindlessCount; }
	/**
	 * @brief Returns compiled pipeline variant count.
	 * @details Specified in the shader with "#variantCount X".
	 */
	uint8 getVariantCount() const noexcept { return variantCount; }
	/**
	 * @brief Is pipeline can be used for multithreaded commands recording.
	 * @details Asynchronous command recording helps to utilize all available CPU cores.
	 */
	bool useAsyncRecording() const noexcept { return asyncRecording; }
	/**
	 * @brief Is pipeline can be used for bindless descriptor set creation.
	 * @details Helps to reduce overhead associated with binding and switching resources like textures, buffers.
	 */
	bool isBindless() const noexcept { return bindless; }

	/*******************************************************************************************************************
	 * @brief Returns push constants data.
	 * @details See the @ref Pipeline::pushConstants()
	 * @tparam T type of the structure with data
	 */
	template<typename T>
	T* getPushConstants()
	{
		GARDEN_ASSERT(!asyncRecording);
		GARDEN_ASSERT(pushConstantsSize == sizeof(T));  // Different shader pushConstants size
		return (T*)pushConstantsBuffer.data();
	}
	/**
	 * @brief Returns constant push constants data.
	 * @details See the @ref Pipeline::pushConstants()
	 * @tparam T type of the structure with data
	 */
	template<typename T>
	const T* getPushConstants() const { return getPushConstants<T>(); }
	
	/**
	 * @brief Returns push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * 
	 * @tparam T type of the structure with data
	 * @param taskIndex index of the thread pool task
	 */
	template<typename T>
	T* getPushConstantsAsync(int32 taskIndex)
	{
		GARDEN_ASSERT(asyncRecording);
		GARDEN_ASSERT(taskIndex >= 0);
		GARDEN_ASSERT(taskIndex < (int32)thread::hardware_concurrency());
		GARDEN_ASSERT(pushConstantsSize == sizeof(T)); // Different shader pushConstants size
		return (T*)(pushConstantsBuffer.data() + pushConstantsSize * taskIndex);
	}
	/**
	 * @brief Returns push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * 
	 * @tparam T type of the structure with data
	 * @param taskIndex index of the thread pool task
	 */
	template<typename T>
	const T* getPushConstantsAsync(int32 taskIndex) const { return getPushConstantsAsync<T>(taskIndex); }

	//******************************************************************************************************************
	// Render commands
	//******************************************************************************************************************

	/**
	 * @brief Binds pipeline for subsequent rendering.
	 * @param variant target shader variant to use or 0 (#variantCount X)
	 * 
	 * @details
	 * Specifies which pipeline state should be active for subsequent rendering commands. This setup is crucial 
	 * because it allows the GPU to prepare for execution of drawing or compute commands that match the specified 
	 * state, optimizing performance by avoiding state changes between rendering command calls whenever possible.
	 */
	void bind(uint8 variant = 0);
	/**
	 * @brief Binds pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bind()
	 * 
	 * @param variant target shaders variant to use or 0 (#variantCount X)
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void bindAsync(uint8 variant = 0, int32 taskIndex = -1);

	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering.
	 * 
	 * @param[in] descriptorSetRange target descriptor set range array
	 * @param rangeCount descriptor set range array size
	 * 
	 * @details
	 * Descriptors are a way of telling the GPU where to find the resources it needs, 
	 * such as textures and buffers, that are used by shaders for rendering or computation.
	 */
	void bindDescriptorSets(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount);
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering.
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param[in] descriptorSetRange target descriptor set range array
	 * @param rangeCount descriptor set range array size
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void bindDescriptorSetsAsync(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount, int32 taskIndex = -1);

	/*******************************************************************************************************************
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering.
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @tparam N descriptor set range array size
	 * @param[in] descriptorSetRange target descriptor set range array
	 */
	template<psize N>
	void bindDescriptorSets(const array<DescriptorSet::Range, N>& descriptorSetRange)
	{
		bindDescriptorSets(descriptorSetRange.data(), (uint8)N);
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering.
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * @param[in] descriptorSetRange target descriptor set range vector
	 */
	void bindDescriptorSets(const vector<DescriptorSet::Range>& descriptorSetRange)
	{
		bindDescriptorSets(descriptorSetRange.data(), (uint8)descriptorSetRange.size());
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering.
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param descriptorSet target descriptor set
	 * @param offset descript set offset in the range or 0
	 */
	void bindDescriptorSet(ID<DescriptorSet> descriptorSet, uint32 offset = 0)
	{
		DescriptorSet::Range descriptorSetRange(descriptorSet, 1, offset);
		bindDescriptorSets(&descriptorSetRange, 1);
	}
	
	/*******************************************************************************************************************
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @tparam N descriptor set range array size
	 * @param[in] descriptorSetRange target descriptor set range array
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	template<psize N>
	void bindDescriptorSetsAsync(const array<DescriptorSet::Range, N>& descriptorSetRange, int32 taskIndex = -1)
	{
		bindDescriptorSetsAsync(descriptorSetRange.data(), (uint8)N, taskIndex);
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param[in] descriptorSetRange target descriptor set range vector
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void bindDescriptorSetsAsync(const vector<DescriptorSet::Range>& descriptorSetRange, int32 taskIndex = -1)
	{
		bindDescriptorSetsAsync(descriptorSetRange.data(), (uint8)descriptorSetRange.size(), taskIndex);
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param descriptorSet target descriptor set
	 * @param offset descript set offset in the range or 0
	 * @param taskIndex index of the thread pool task (-1 = all threads)
	 */
	void bindDescriptorSetAsync(ID<DescriptorSet> descriptorSet, uint32 offset = 0, int32 taskIndex = -1)
	{
		DescriptorSet::Range descriptorSetRange(descriptorSet, 1, offset);
		bindDescriptorSetsAsync(&descriptorSetRange, 1, taskIndex);
	}
	
	/*******************************************************************************************************************
	 * @brief Pushes specified constants for subsequent rendering.
	 * 
	 * @details
	 * Allow for rapid updating of shader data without the overhead associated with other resource updates like 
	 * uniform buffers or descriptor sets. They are typically used to pass small-sized data such as 
	 * transformation matrices, lighting parameters or simple control variables directly to shaders.
	 */
	void pushConstants();
	/**
	 * @brief Pushes specified constants for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * @param taskIndex index of the thread pool task
	 */
	void pushConstantsAsync(int32 taskIndex);
};

/**
 * @brief Returns sampler wrap mode.
 * @param samplerWrap target sampler wrap mode name string (camelCase)
 * @throw runtime_error on unknown sampler wrap mode.
 */
static Pipeline::SamplerWrap toSamplerWrap(string_view samplerWrap)
{
	if (samplerWrap == "repeat") return Pipeline::SamplerWrap::Repeat;
	if (samplerWrap == "mirroredRepeat") return Pipeline::SamplerWrap::MirroredRepeat;
	if (samplerWrap == "clampToEdge") return Pipeline::SamplerWrap::ClampToEdge;
	if (samplerWrap == "clampToBorder") return Pipeline::SamplerWrap::ClampToBorder;
	if (samplerWrap == "mirrorClampToEdge") return Pipeline::SamplerWrap::MirrorClampToEdge;
	throw runtime_error("Unknown sampler wrap type. (" + string(samplerWrap) + ")");
}
/**
 * @brief Returns border color type.
 * @param borderColor target border color name string (camelCase)
 * @throw runtime_error on unknown border color type.
 */
static Pipeline::BorderColor toBorderColor(string_view borderColor)
{
	if (borderColor == "floatTransparentBlack") return Pipeline::BorderColor::FloatTransparentBlack;
	if (borderColor == "intTransparentBlack") return Pipeline::BorderColor::IntTransparentBlack;
	if (borderColor == "floatOpaqueBlack") return Pipeline::BorderColor::FloatOpaqueBlack;
	if (borderColor == "intOpaqueBlack") return Pipeline::BorderColor::IntOpaqueBlack;
	if (borderColor == "floatOpaqueWhite") return Pipeline::BorderColor::FloatOpaqueWhite;
	if (borderColor == "intOpaqueWhite") return Pipeline::BorderColor::IntOpaqueWhite;
	throw runtime_error("Unknown border color type. (" + string(borderColor) + ")");
}
/**
 * @brief Returns comparison operator type.
 * @param compareOperation target comparison operator name string (camelCase)
 * @throw runtime_error on unknown comparison operator type.
 */
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
	throw runtime_error("Unknown compare operation type. (" + string(compareOperation) + ")");
}

/***********************************************************************************************************************
 * @brief Sampler wrap mode name strings.
 */
static const string_view samplerWrapNames[(psize)Pipeline::SamplerWrap::Count] =
{
	"Repeat", "MirroredRepeat", "ClampToEdge", "ClampToBorder", "MirrorClampToEdge"
};
/**
 * @brief Border color name strings.
 */
static const string_view borderColorNames[(psize)Pipeline::BorderColor::Count] =
{
	"FloatTransparentBlack", "IntTransparentBlack", "FloatOpaqueBlack",
	"IntOpaqueBlack", "FloatOpaqueWhite", "IntOpaqueWhite"
};
/**
 * @brief Compare operation name strings.
 */
static const string_view compareOperationNames[(psize)Pipeline::CompareOperation::Count] =
{
	"Never", "Less", "Equal", "LessOrEqual", "Greater", "NotEqual", "GreaterOrEqual", "Always"
};

/**
 * @brief Returns sampler wrap mode name string.
 * @param memoryAccess target sampler wrap mode
 */
static string_view toString(Pipeline::SamplerWrap samplerWrap) noexcept
{
	GARDEN_ASSERT((uint8)samplerWrap < (uint8)Pipeline::SamplerWrap::Count);
	return samplerWrapNames[(psize)samplerWrap];
}
/**
 * @brief Returns border color name string.
 * @param memoryAccess target border color type
 */
static string_view toString(Pipeline::BorderColor borderColor) noexcept
{
	GARDEN_ASSERT((uint8)borderColor < (uint8)Pipeline::BorderColor::Count);
	return borderColorNames[(psize)borderColor];
}
/**
 * @brief Returns comparison operator name string.
 * @param memoryAccess target comparison operator type
 */
static string_view toString(Pipeline::CompareOperation compareOperation) noexcept
{
	GARDEN_ASSERT((uint8)compareOperation < (uint8)Pipeline::CompareOperation::Count);
	return compareOperationNames[(psize)compareOperation];
}

/***********************************************************************************************************************
 * @brief Rendering pipeline resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class PipelineExt final
{
public:
	/**
	 * @brief Returns pipeline uniform map.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static map<string, Pipeline::Uniform>& getUniforms(Pipeline& pipeline) noexcept { return pipeline.uniforms; }
	/**
	 * @brief Returns pipeline push constants buffer.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static vector<uint8>& getPushConstantsBuffer(Pipeline& pipeline) noexcept { return pipeline.pushConstantsBuffer; }
	/**
	 * @brief Returns pipeline sampler array.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static vector<void*>& getSamplers(Pipeline& pipeline) noexcept { return pipeline.samplers; }
	/**
	 * @brief Returns pipeline descriptor set layout array.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static vector<void*>& getDescriptorSetLayouts(Pipeline& pipeline) noexcept { return pipeline.descriptorSetLayouts; }
	/**
	 * @brief Returns pipeline descriptor set pool array.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static vector<void*>& getDescriptorPools(Pipeline& pipeline) noexcept { return pipeline.descriptorPools; }
	/**
	 * @brief Returns pipeline resource path.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static fs::path& getPath(Pipeline& pipeline) noexcept { return pipeline.pipelinePath; }
	/**
	 * @brief Returns pipeline layout instance.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static void*& getLayout(Pipeline& pipeline) noexcept { return pipeline.pipelineLayout; }
	/**
	 * @brief Returns pipeline instance version.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static uint64& getVersion(Pipeline& pipeline) noexcept { return pipeline.pipelineVersion; }
	/**
	 * @brief Returns pipeline maximum bindless descriptor count in the array.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static uint32& getMaxBindlessCount(Pipeline& pipeline) noexcept { return pipeline.maxBindlessCount; }
	/**
	 * @brief Returns pipeline push constants shader stages mask.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static uint32& getPushConstantsMask(Pipeline& pipeline) noexcept { return pipeline.pushConstantsMask; }
	/**
	 * @brief Returns pipeline push constants buffer size in bytes.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static uint16& getPushConstantsSize(Pipeline& pipeline) noexcept { return pipeline.pushConstantsSize; }
	/**
	 * @brief Returns rendering pipeline type.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static PipelineType& getType(Pipeline& pipeline) noexcept { return pipeline.type; }
	/**
	 * @brief Returns compiled pipeline variant count.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static uint8& getVariantCount(Pipeline& pipeline) noexcept { return pipeline.variantCount; }
	/**
	 * @brief Is pipeline can be used for multithreaded commands recording.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static bool& isAsyncRecording(Pipeline& pipeline) noexcept { return pipeline.asyncRecording; }
	/**
	 * @brief Is pipeline can be used for bindless descriptor set creation.
	 * @warning In most cases you should use @ref Pipeline functions.
	 * @param[in] pipeline target pipeline instance
	 */
	static bool& isBindless(Pipeline& pipeline) noexcept { return pipeline.bindless; }
	
	/*******************************************************************************************************************
	 * @brief Moves internal pipeline objects.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * 
	 * @param[in,out] source source pipeline instance
	 * @param[in,out] destination destination pipeline instance
	 */
	static void moveInternalObjects(Pipeline& source, Pipeline& destination) noexcept
	{
		PipelineExt::getUniforms(destination) = std::move(PipelineExt::getUniforms(source));
		PipelineExt::getPushConstantsBuffer(destination) = std::move(PipelineExt::getPushConstantsBuffer(source));
		PipelineExt::getSamplers(destination) = std::move(PipelineExt::getSamplers(source));
		PipelineExt::getDescriptorSetLayouts(destination) = std::move(PipelineExt::getDescriptorSetLayouts(source));
		PipelineExt::getDescriptorPools(destination) = std::move(PipelineExt::getDescriptorPools(source));
		PipelineExt::getLayout(destination) = PipelineExt::getLayout(source);
		PipelineExt::getPushConstantsMask(destination) = PipelineExt::getPushConstantsMask(source);
		PipelineExt::getPushConstantsSize(destination) = PipelineExt::getPushConstantsSize(source);
		PipelineExt::getVariantCount(destination) = PipelineExt::getVariantCount(source);
		PipelineExt::isBindless(destination) = PipelineExt::isBindless(source);
		ResourceExt::getInstance(destination) = ResourceExt::getInstance(source);
		ResourceExt::getInstance(source) = nullptr;
	}
	/**
	 * @brief Destroys pipeline instance.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 * @param[in,out] pipeline target pipeline instance
	 */
	static void destroy(Pipeline& pipeline) { pipeline.destroy(); }
};

} // namespace garden::graphics
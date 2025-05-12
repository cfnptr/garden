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
 * @brief Common rendering pipeline functions.
 */

#pragma once
#include "garden/graphics/gsl.hpp"
#include "garden/graphics/descriptor-set.hpp"

namespace garden::graphics
{

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
		ShaderStage shaderStages = {}; /**< Shader stages where uniform is used. */
		GslUniformType type = {};      /**< Uniform variable type. */
		uint8 bindingIndex = 0;        /**< Binding index inside the descriptor set. */
		uint8 descriptorSetIndex = 0;  /**< Index of the descriptor set. */
		uint8 arraySize = 0;           /**< Number of descriptors contained in the binding. */
		bool readAccess = true;        /**< Is variable read access allowed. */
		bool writeAccess = true;       /**< Is variable write access allowed. */
		bool isMutable = false;        /**< Is uniform resource can be assigned dynamically. */
		uint8 _alignment = 0;          // Note: Should be aligned.
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
		uint16 _alignment = 0;
	};

	struct SpecConstBase { GslDataType type = {}; uint32 data = 0; };
	struct SpecConstBool { GslDataType type = {}; uint32 value = false; };
	struct SpecConstInt32 { GslDataType type = {}; int32 value = 0; };
	struct SpecConstUint32 { GslDataType type = {}; uint32 value = 0; };
	struct SpecConstFloat { GslDataType type = {}; float value = 0.0f; };

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

		SpecConstValue() { constInt32.value = 0; }
		SpecConstValue(bool value) { constBool.value = value; constBool.type = GslDataType::Bool; }
		SpecConstValue(int32 value) { constInt32.value = value; constBool.type = GslDataType::Int32; }
		SpecConstValue(uint32 value) { constUint32.value = value; constBool.type = GslDataType::Uint32; }
		SpecConstValue(float value) { constFloat.value = value; constBool.type = GslDataType::Float; }
	};

	using SamplerStates = tsl::robin_map<string, Sampler::State>;
	using Uniforms = tsl::robin_map<string, Uniform, SvHash, SvEqual>;
	using SpecConsts = tsl::robin_map<string, SpecConst>;
	using SpecConstValues = tsl::robin_map<string, SpecConstValue>;

	/*******************************************************************************************************************
	 * @brief Rendering pipeline create data container.
	 * @warning In most cases you should use @ref GraphicsSystem functions.
	 */
	struct CreateData
	{
		SamplerStates samplerStates;
		Uniforms uniforms;
		SpecConsts specConsts;
		SpecConstValues specConstValues;
		SamplerStates samplerStateOverrides;
		vector<uint8> headerData;
		fs::path shaderPath;
		uint64 pipelineVersion = 0;
		uint32 maxBindlessCount = 0;
		ShaderStage pushConstantsStages = {};
		uint16 pushConstantsSize = 0;
		uint8 descriptorSetCount = 0;
		uint8 variantCount = 0;
	};
protected:
	Uniforms uniforms;
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

	Pipeline(CreateData& createData, bool useAsyncRecording);
	Pipeline(PipelineType type, const fs::path& path, uint32 maxBindlessCount, bool useAsyncRecording, 
		uint64 pipelineVersion) : pipelinePath(path), pipelineVersion(pipelineVersion),
		maxBindlessCount(maxBindlessCount), type(type), asyncRecording(useAsyncRecording)
	{
		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (type == PipelineType::Graphics)
			debugName = "graphicsPipeline." + path.generic_string();
		else if (type == PipelineType::Compute)
			debugName = "computePipeline." + path.generic_string();
		else if (type == PipelineType::RayTracing)
			debugName = "rayTracingPipeline." + path.generic_string();
		else abort();
		#endif
	}
	bool destroy() final;

	static vector<void*> createShaders(const vector<uint8>* codeArray, uint8 shaderCount, const fs::path& path);
	static void destroyShaders(const vector<void*>& shaders);

	static void fillVkSpecConsts(const fs::path& path, void* specInfo, const SpecConsts& specConsts, 
		const SpecConstValues& specConstValues, ShaderStage shaderStage, uint8 variantCount);
	static void setVkVariantIndex(void* specInfo, uint8 variantIndex);
	static void updateDescriptorsLock(const DescriptorSet::Range* descriptorSetRange, uint8 descriptorDataSize);
	static bool checkThreadIndex(int32 threadIndex);

	friend class PipelineExt;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty pipeline data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access pipelines.
	 */
	Pipeline() = default;

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
	const Uniforms& getUniforms() const noexcept { return uniforms; }
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
	 * @brief Returns push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * 
	 * @param threadIndex thread index in the pool
	 * @tparam T type of the structure with data
	 */
	template<typename T>
	T* getPushConstants(int32 threadIndex = 0)
	{
		GARDEN_ASSERT(checkThreadIndex(threadIndex));
		GARDEN_ASSERT(pushConstantsSize == sizeof(T));  // Different shader pushConstants size
		return (T*)(pushConstantsBuffer.data() + pushConstantsSize * threadIndex);
	}
	/**
	 * @brief Returns constant push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * 
	 * @param threadIndex thread index in the pool
	 * @tparam T type of the structure with data
	 */
	template<typename T>
	const T* getPushConstants(int32 threadIndex = 0) const { return getPushConstants<T>(); }

	/**
	 * @brief Returns push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * @param threadIndex thread index in the pool
	 */
	uint8* getPushConstants(int32 threadIndex = 0)
	{
		GARDEN_ASSERT(asyncRecording);
		GARDEN_ASSERT(checkThreadIndex(threadIndex));
		return pushConstantsBuffer.data() + pushConstantsSize * threadIndex;
	}
	/**
	 * @brief Returns push constants data. (MT-Safe)
	 * @details See the @ref Pipeline::pushConstants()
	 * @param threadIndex thread index in the pool
	 */
	const uint8* getPushConstants(int32 threadIndex = 0) const
	{
		GARDEN_ASSERT(asyncRecording);
		GARDEN_ASSERT(checkThreadIndex(threadIndex));
		return pushConstantsBuffer.data() + pushConstantsSize * threadIndex;
	}

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
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void bindAsync(uint8 variant, int32 threadIndex = -1);

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
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void bindDescriptorSetsAsync(const DescriptorSet::Range* descriptorSetRange, uint8 rangeCount, int32 threadIndex = -1);

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
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	template<psize N>
	void bindDescriptorSetsAsync(const array<DescriptorSet::Range, N>& descriptorSetRange, int32 threadIndex = -1)
	{
		bindDescriptorSetsAsync(descriptorSetRange.data(), (uint8)N, threadIndex);
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param[in] descriptorSetRange target descriptor set range vector
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void bindDescriptorSetsAsync(const vector<DescriptorSet::Range>& descriptorSetRange, int32 threadIndex = -1)
	{
		bindDescriptorSetsAsync(descriptorSetRange.data(), (uint8)descriptorSetRange.size(), threadIndex);
	}
	/**
	 * @brief Binds descriptor set range to this pipeline for subsequent rendering. (MT-Safe)
	 * @details See the @ref Pipeline::bindDescriptorSets()
	 * 
	 * @param descriptorSet target descriptor set
	 * @param offset descript set offset in the range or 0
	 * @param threadIndex thread index in the pool (-1 = all threads)
	 */
	void bindDescriptorSetAsync(ID<DescriptorSet> descriptorSet, uint32 offset, int32 threadIndex = -1)
	{
		DescriptorSet::Range descriptorSetRange(descriptorSet, 1, offset);
		bindDescriptorSetsAsync(&descriptorSetRange, 1, threadIndex);
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
	 * @param threadIndex thread index in the pool
	 */
	void pushConstantsAsync(int32 threadIndex);
};

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
	static Pipeline::Uniforms& getUniforms(Pipeline& pipeline) noexcept { return pipeline.uniforms; }
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
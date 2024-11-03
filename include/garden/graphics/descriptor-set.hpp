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
 * @brief Common graphics descriptor set functions.
 */

#pragma once
#include "linear-pool.hpp"
#include "garden/graphics/common.hpp"
#include "garden/graphics/resource.hpp"

#include <map>
#include <typeindex>

namespace garden::graphics
{

using namespace ecsm;
class Pipeline;
class DescriptorSetExt;

/***********************************************************************************************************************
 * @brief Shader resource container.
 * 
 * @details
 * Descriptor set is a mechanism for binding application resources, such as buffers and images, to the shader stages 
 * in a pipeline. It acts as a bridge between the resources you have in your application (like textures, 
 * uniform buffers and samplers) and the shader programs that use those resources when drawing or computing. 
 * Descriptors are part of way to abstract resource bindings and provide a highly efficient, explicit, 
 * and flexible way to manage resource states and dependencies.
 */
class DescriptorSet final : public Resource
{
public:
	/*******************************************************************************************************************
	 * @brief Descriptor set resource container.
	 * @details Resources like buffers, images and other types of data that shaders need to access.
	 */
	struct Uniform final
	{
		/**
		 * @brief Uniform resource array or just one resource.
		 */
		using ResourceArray = vector<ID<Resource>>;

		/**
		 * @brief Resource array for each descriptor set.
		 */
		vector<ResourceArray> resourceSets;
	private:
		#if GARDEN_DEBUG
		type_index type;
		#endif
	public:
		/**
		 * @brief Creates a new descriptor set uniform out of target resource.
		 * 
		 * @tparam T type of the resource (or @ref Resource)
		 * @param resource target uniform resource
		 * @param arraySize resource array size
		 * @param setCount descriptor set count
		 */
		template<class T = Resource>
		Uniform(ID<T> resource, psize arraySize = 1, psize setCount = 1) noexcept :
			resourceSets(setCount, ResourceArray(arraySize, ID<Resource>(resource)))
			#if GARDEN_DEBUG
			, type(typeid(T))
			#endif
		{ }
		/**
		 * @brief Creates a new descriptor set uniform out of resource sets.
		 * 
		 * @tparam T type of the resource (or @ref Resource)
		 * @param[in] resourceSets target resource sets
		 */
		template<class T = Resource>
		Uniform(const vector<vector<ID<T>>>& resourceSets) noexcept :
			resourceSets(*((const vector<ResourceArray>*)&resourceSets))
			#if GARDEN_DEBUG
			, type(typeid(T))
			#endif
		{ }
		/**
		 * @brief Creates a new empty descriptor set uniform.
		 * @note It can not be used to create a descriptor set.
		 */
		Uniform() noexcept
			#if GARDEN_DEBUG
			: type(typeid(Resource)) { }
			#else
			= default;
			#endif

		#if GARDEN_DEBUG
		type_index getDebugType() const noexcept { return type; }
		#endif
	};

	/*******************************************************************************************************************
	 * @brief Descriptor set range description.
	 * @details Defines range for descriptor set binding.
	 */
	struct Range final
	{
		ID<DescriptorSet> set; /**< Target descriptor set to bind. */
		uint32 count = 0;      /**< Descriptor set count to bind. */
		uint32 offset = 0;     /**< Descriptor set offset in the array. */

		/**
		 * @brief Creates a new descriptor set range description.
		 * 
		 * @param set target descriptor set to bind
		 * @param count descriptor set count to bind
		 * @param offset descriptor set offset in the array or 0
		 */
		constexpr Range(ID<DescriptorSet> set, uint32 count = 1, uint32 offset = 0) noexcept :
			set(set), count(count), offset(offset) { }
		/**
		 * @brief Creates a new empty descriptor set range description.
		 * @note It can not be used to bind descriptor set range.
		 */
		constexpr Range() = default;
	};
private:
	ID<Pipeline> pipeline = {};
	map<string, Uniform> uniforms;
	PipelineType pipelineType = {};
	uint8 index = 0;

	// Note: Use GraphicsSystem to create, destroy and access descriptor sets.

	DescriptorSet() = default;
	DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType,
		map<string, Uniform>&& uniforms, uint8 index);
	bool destroy() final;

	friend class DescriptorSetExt;
	friend class LinearPool<DescriptorSet>;
public:
	/**
	 * @brief Returns descriptor set parent pipeline.
	 * @note Can be used only with parent pipeline.
	 */
	ID<Pipeline> getPipeline() const noexcept { return pipeline; }
	/**
	 * @brief Returns descriptor set parent pipeline type.
	 * @note Can be used only with parent pipeline.
	 */
	PipelineType getPipelineType() const noexcept { return pipelineType; }
	/**
	 * @brief Returns descriptor set index inside the shader.
	 * @details Specified using setX keyword "uniform set1 sampler2D...".
	 */
	uint8 getIndex() const noexcept { return index; }
	/**
	 * @brief Returns uniform map. (resources)
	 * @details Can be used to access descriptor set resources.
	 */
	const map<string, Uniform>& getUniforms() const noexcept { return uniforms; }
	/**
	 * @brief Returns internal descriptor set instance count.
	 * @details Internally single set descriptor can contain multiple instances.
	 */
	uint32 getSetCount() const noexcept
	{
		if (uniforms.empty())
			return 0;
		return (uint32)uniforms.begin()->second.resourceSets.size();
	}

	/**
	 * @brief Recreates descriptor set with a new resources.
	 * @param[in] uniforms new resource sets
	 * @warning Use only when required, this operation impacts performance!
	 */
	void recreate(map<string, Uniform>&& uniforms);

	// TODO: void copy(ID<DescriptorSet> descriptorSet);

	/**
	 * @brief Updates specific descriptor set uniform.
	 * @details Useful for updating bindless descriptor set resources.
	 * 
	 * @param[in] name target uniform name
	 * @param[in] uniform new resource sets
	 * @param elementOffset element offset inside descriptor set or 0
	 * 
	 * @warning Use only when required, this operation impacts performance!
	 */
	void updateUniform(const string& name, const Uniform& uniform, uint32 elementOffset = 0);

	#if GARDEN_DEBUG
	/**
	 * @brief Sets descriptor set debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif
};

/***********************************************************************************************************************
 * @brief Graphics descriptor set resource extension mechanism.
 * @warning Use only if you know what you are doing!
 */
class DescriptorSetExt final
{
public:
	/**
	 * @brief Returns descriptor set parent pipeline.
	 * @warning In most cases you should use @ref DescriptorSet functions.
	 * @param[in] descriptorSet target descriptor set instance
	 */
	static ID<Pipeline>& getPipeline(DescriptorSet& descriptorSet) noexcept { return descriptorSet.pipeline; }
	/**
	 * @brief Returns descriptor set uniform map. (resources)
	 * @warning In most cases you should use @ref DescriptorSet functions.
	 * @param[in] descriptorSet target descriptor set instance
	 */
	static map<string, DescriptorSet::Uniform>& getUniforms(DescriptorSet& descriptorSet)
		noexcept { return descriptorSet.uniforms; }
	/**
	 * @brief Returns descriptor set parent pipeline type.
	 * @warning In most cases you should use @ref DescriptorSet functions.
	 * @param[in] descriptorSet target descriptor set instance
	 */
	static PipelineType& getPipelineType(DescriptorSet& descriptorSet) noexcept { return descriptorSet.pipelineType; }
	/**
	 * @brief Returns descriptor set index inside the shader.
	 * @warning In most cases you should use @ref DescriptorSet functions.
	 * @param[in] descriptorSet target descriptor set instance
	 */
	static uint8& getIndex(DescriptorSet& descriptorSet) noexcept { return descriptorSet.index; }
};

} // namespace garden::graphics
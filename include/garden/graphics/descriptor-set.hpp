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
class GraphicsPipeline;

/***********************************************************************************************************************
 * @brief Shader resource container.
 * 
 * @details
 * Descriptor set is a mechanism for binding application resources, such as buffers and images, to the shader stages 
 * in a pipeline. It acts as a bridge between the resources you have in your application (like textures, 
 * uniform buffers, and samplers) and the shader programs that use those resources when drawing or computing. 
 * Descriptors are part of Vulkan's way to abstract resource bindings and provide a highly efficient, 
 * explicit, and flexible way to manage resource states and dependencies.
 */
class DescriptorSet final : public Resource
{
public:
	struct Uniform final
	{
		using ResourceArray = vector<ID<Resource>>;
		vector<ResourceArray> resourceSets;
	private:
		#if GARDEN_DEBUG
		type_index type;
		#endif
		friend class DescriptorSet;
	public:
		template<class T = Resource>
		Uniform(ID<T> resource, psize arraySize = 1, psize setCount = 1) :
			resourceSets(setCount, ResourceArray(arraySize, ID<Resource>(resource)))
			#if GARDEN_DEBUG
			, type(typeid(T))
			#endif
		{ }
		template<class T = Resource>
		Uniform(const vector<vector<ID<T>>>& resourceSets) :
			resourceSets(*((const vector<ResourceArray>*)&resourceSets))
			#if GARDEN_DEBUG
			, type(typeid(T))
			#endif
		{ }
		Uniform()
			#if GARDEN_DEBUG
			: type(typeid(Resource)) { }
			#else
			= default;
			#endif
	};
//--------------------------------------------------------------------------------------------------
private:
	ID<Pipeline> pipeline = {};
	map<string, Uniform> uniforms;
	PipelineType pipelineType = {};
	uint8 index = 0;

	// Use GraphicsSystem to create, destroy and access descriptor sets.

	DescriptorSet() = default;
	DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType,
		map<string, Uniform>&& uniforms, uint8 index);
	bool destroy() final;

	friend class Vulkan;
	friend class Pipeline;
	friend class CommandBuffer;
	friend class LinearPool<DescriptorSet>;
public:
	ID<Pipeline> getPipeline() const noexcept { return pipeline; }
	PipelineType getPipelineType() const noexcept { return pipelineType; }
	uint8 getIndex() const noexcept { return index; }
	const map<string, Uniform>& getUniforms() const noexcept { return uniforms; }
	
	uint32 getSetCount() const noexcept
	{
		if (uniforms.empty())
			return 0;
		return (uint32)uniforms.begin()->second.resourceSets.size();
	}

	void recreate(map<string, Uniform>&& uniforms);
	// TODO: void copy(ID<DescriptorSet> descriptorSet);

	void updateUniform(const string& name,
		const Uniform& uniform, uint32 elementOffset = 0);

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif
};

} // namespace garden::graphics
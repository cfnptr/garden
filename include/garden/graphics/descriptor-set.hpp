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

//--------------------------------------------------------------------------------------------------
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
	
	uint32 getSetCount() const noexcept {
		return (uint32)uniforms.begin()->second.resourceSets.size(); }

	void recreate(map<string, Uniform>&& uniforms);
	// TODO: void copy(ID<DescriptorSet> descriptorSet);

	void updateUniform(const string& name,
		const Uniform& uniform, uint32 elementOffset = 0);

	#if GARDEN_DEBUG
	void setDebugName(const string& name) final;
	#endif
};

} // garden::graphics
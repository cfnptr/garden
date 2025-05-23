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
 * @brief Common graphics descriptor set functions.
 */

#pragma once
#include "garden/graphics/common.hpp"
#include "garden/graphics/image.hpp"
#include "garden/graphics/acceleration-structure/tlas.hpp"
#include "tsl/robin_map.h"

namespace garden::graphics
{

class Pipeline;
class DescriptorSetExt;

struct SvHash
{
	using is_transparent = void;
	std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
	std::size_t operator()(const std::string& str) const { return std::hash<std::string>{}(str); }
};
struct SvEqual
{
	using is_transparent = void;
	bool operator()(std::string_view lhs, std::string_view rhs) const noexcept { return lhs == rhs; }
};

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
	 * @brief Descriptor set uniform resources container.
	 * @details Resources like buffers, images and other types of data that shaders need to access.
	 */
	struct Uniform final
	{
		using ResourceArray = vector<ID<Resource>>; /**< Uniform resource array or just one resource. */
		vector<ResourceArray> resourceSets;         /**< Resource array for each descriptor set. */
	public:
		/**
		 * @brief Creates a new descriptor set uniform out of target buffer.
		 * 
		 * @param buffer target buffer instance
		 * @param resourceCount resource array size
		 * @param setCount descriptor set count
		 */
		Uniform(ID<Buffer> buffer, psize resourceCount = 1, psize setCount = 1) noexcept :
			resourceSets(setCount, ResourceArray(resourceCount, ID<Resource>(buffer))) { }
		/**
		 * @brief Creates a new descriptor set uniform out of target image view.
		 * 
		 * @param imageView target image view instance
		 * @param resourceCount resource array size
		 * @param setCount descriptor set count
		 */
		Uniform(ID<ImageView> imageView, psize resourceCount = 1, psize setCount = 1) noexcept :
			resourceSets(setCount, ResourceArray(resourceCount, ID<Resource>(imageView))) { }
		/**
		 * @brief Creates a new descriptor set uniform out of target TLAS.
		 * 
		 * @param tlas target top level acceleration structure instance (TLAS)
		 * @param resourceCount resource array size
		 * @param setCount descriptor set count
		 */
		Uniform(ID<Tlas> tlas, psize resourceCount = 1, psize setCount = 1) noexcept :
			resourceSets(setCount, ResourceArray(resourceCount, ID<Resource>(tlas))) { }

		/**
		 * @brief Creates a new descriptor set uniform out of buffers.
		 * @param[in] buffers target buffer array
		 */
		Uniform(const vector<vector<ID<Buffer>>>& buffers) noexcept :
			resourceSets(*((const vector<ResourceArray>*)&buffers)) { }
		/**
		 * @brief Creates a new descriptor set uniform out of image views.
		 * @param[in] imageViews target image view array
		 */
		Uniform(const vector<vector<ID<ImageView>>>& imageViews) noexcept :
			resourceSets(*((const vector<ResourceArray>*)&imageViews)) { }
		/**
		 * @brief Creates a new descriptor set uniform out of image views.
		 * @param[in] tlases target top level acceleration structure array (TLAS)
		 */
		Uniform(const vector<vector<ID<Tlas>>>& tlases) noexcept :
			resourceSets(*((const vector<ResourceArray>*)&tlases)) { }

		/**
		 * @brief Creates a new empty descriptor set uniform.
		 */
		Uniform() noexcept = default;
	};

	/*******************************************************************************************************************
	 * @brief Descriptor set uniform resource container.
	 * @details See the @ref Uniform for details.
	 */
	struct UniformResource final
	{
		ID<Resource> resource = {}; /**< Uniform resource instance. */
	public:
		/**
		 * @brief Creates a new descriptor set uniform out of target buffer.
		 * @param buffer target buffer instance
		 */
		UniformResource(ID<Buffer> buffer) noexcept : resource(ID<Resource>(buffer)) { }
		/**
		 * @brief Creates a new descriptor set uniform out of target image view.
		 * @param imageView target image view instance
		 */
		UniformResource(ID<ImageView> imageView) noexcept : resource(ID<Resource>(imageView)) { }
		/**
		 * @brief Creates a new descriptor set uniform out of target TLAS.
		 * @param tlas target top level acceleration structure instance (TLAS)
		 */
		UniformResource(ID<Tlas> tlas) noexcept : resource(ID<Resource>(tlas)) { }
		/**
		 * @brief Creates a new empty descriptor set uniform.
		 * @note It can not be used to create a descriptor set.
		 */
		UniformResource() noexcept = default;
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

	using Uniforms = tsl::robin_map<string, Uniform, SvHash, SvEqual>;
	using Samplers = tsl::robin_map<string, ID<Sampler>>;
private:
	ID<Pipeline> pipeline = {};
	Uniforms uniforms;
	PipelineType pipelineType = {};
	uint8 index = 0;
	uint8 setCount = 0;

	DescriptorSet(ID<Pipeline> pipeline, PipelineType pipelineType,
		Uniforms&& uniforms, Samplers&& samplers, uint8 index);
	bool destroy() final;

	friend class DescriptorSetExt;
	friend class LinearPool<DescriptorSet>;
public:
	/*******************************************************************************************************************
	 * @brief Creates a new empty descriptor set data container.
	 * @note Use @ref GraphicsSystem to create, destroy and access descriptor sets.
	 */
	DescriptorSet() = default;

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
	const Uniforms& getUniforms() const noexcept { return uniforms; }
	/**
	 * @brief Returns uniform map. (resources)
	 * @details Can be used to access descriptor set resources.
	 */
	Uniforms& getUniforms() noexcept { return uniforms; }
	/**
	 * @brief Returns internal descriptor set instance count.
	 * @details Internally single descriptor set can contain multiple instances.
	 */
	uint32 getSetCount() const noexcept { return setCount; }

	/**
	 * @brief Recreates descriptor set with a new resources.
	 *
	 * @param[in] uniforms new resource sets
	 * @param[in] samplers dynamic samplers (mutable uniforms)
	 *
	 * @warning Use only when required, this operation impacts performance!
	 */
	void recreate(Uniforms&& uniforms, Samplers&& samplers = {});

	// TODO: void copy(ID<DescriptorSet> descriptorSet);

	/**
	 * @brief Updates specific descriptor set uniform resource.
	 * @details Useful for updating bindless descriptor set resources.
	 * 
	 * @param name target uniform name
	 * @param[in] uniform new descriptor set uniform resource
	 * @param elementIndex element index inside descriptor array
	 * @param setIndex descriptor set index inside descriptor set array
	 */
	void updateUniform(string_view name, const UniformResource& uniform, 
		uint32 elementIndex = 0, uint32 setIndex = 0);
	/**
	 * @brief Writes updated descriptor set uniform resources.
	 * @warning Use only when required, this operation impacts performance!
	 *
	 * @param name target uniform name
	 * @param elementCount descriptor array element count
	 * @param elementOffset element offset inside descriptor array
	 * @param setIndex descriptor set index inside descriptor set array
	 */
	void updateResources(string_view name, uint32 elementCount, uint32 elementOffset = 0, uint32 setIndex = 0);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	/**
	 * @brief Sets descriptor set debug name. (Debug Only)
	 * @param[in] name target debug name
	 */
	void setDebugName(const string& name) final;
	#endif

	#if GARDEN_DEBUG || GARDEN_EDITOR
	static uint32 combinedSamplerCount; /**< Total descriptor pool combined sampler count. */
	static uint32 uniformBufferCount;   /**< Total descriptor pool uniform buffer count. */
	static uint32 storageImageCount;    /**< Total descriptor pool storage image count. */
	static uint32 storageBufferCount;   /**< Total descriptor pool storage buffer count. */
	static uint32 inputAttachmentCount; /**< Total descriptor pool input attachment count. */
	static uint32 accelStructureCount;  /**< Total descriptor pool acceleration structure count. */
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
	static DescriptorSet::Uniforms& getUniforms(DescriptorSet& descriptorSet) noexcept { return descriptorSet.uniforms; }
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
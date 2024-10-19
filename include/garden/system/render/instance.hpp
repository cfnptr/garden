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
 * @brief Common mesh instance rendering functions.
 */

#pragma once
#include "garden/system/render/mesh.hpp"

namespace garden
{

/**
 * @brief General mesh instance rendering system.
 */
class InstanceRenderSystem : public System, public IMeshRenderSystem
{
protected:
	vector<vector<ID<Buffer>>> instanceBuffers = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> baseDescriptorSet = {};
	ID<DescriptorSet> defaultDescriptorSet = {};
	uint32 swapchainIndex = 0;
	View<GraphicsPipeline> pipelineView = {};
	uint8* instanceMap = nullptr;
	bool useBaseDS = false;
	bool useDefaultDS = false;

	/**
	 * @brief Creates a new instance rendering system instance.
	 * 
	 * @param useBaseDS use base descriptor set
	 * @param useDefaultDS use default descriptor set
	 */
	InstanceRenderSystem(bool useBaseDS = true, bool useDefaultDS = true);
	/**
	 * @brief Destroys instance rendering system instance.
	 */
	~InstanceRenderSystem() override;

	virtual void init();
	virtual void deinit();
	virtual void swapchainRecreate();
	
	bool isDrawReady() override;
	void prepareDraw(const float4x4& viewProj, uint32 drawCount) override;
	void beginDrawAsync(int32 taskIndex) override;
	void finalizeDraw(const float4x4& viewProj, uint32 drawCount) override;

	virtual void setDescriptorSetRange(MeshRenderComponent* meshRenderView,
		DescriptorSet::Range* range, uint8& index, uint8 capacity);
	virtual map<string, DescriptorSet::Uniform> getBaseUniforms();
	virtual map<string, DescriptorSet::Uniform> getDefaultUniforms() { return {}; }
	virtual ID<GraphicsPipeline> createPipeline() = 0;

	#if GARDEN_DEBUG
	void setInstancesBuffersName(const string& debugName);
	#endif
public:
	/**
	 * @breif Returns system graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
	/**
	 * @brief Returns system mesh instance data size in bytes.
	 */
	virtual uint64 getInstanceDataSize() = 0;
};

} // namespace garden
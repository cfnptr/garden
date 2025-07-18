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
	DescriptorSet::Buffers baseInstanceBuffers = {};
	DescriptorSet::Buffers shadowInstanceBuffers = {};
	ID<GraphicsPipeline> basePipeline = {};
	ID<GraphicsPipeline> shadowPipeline = {};
	ID<DescriptorSet> baseDescriptorSet = {};
	ID<DescriptorSet> shadowDescriptorSet = {};
	uint32 inFlightIndex = 0;
	uint32 shadowDrawIndex = 0;
	ID<DescriptorSet> descriptorSet = {};
	View<GraphicsPipeline> pipelineView = {};
	uint8* instanceMap = nullptr;

	/**
	 * @brief Creates a new mesh instance rendering system instance.
	 */
	InstanceRenderSystem();
	/**
	 * @brief Destroys mesh instance rendering system instance.
	 */
	~InstanceRenderSystem() override;

	virtual void init();
	virtual void deinit();
	virtual void gBufferRecreate();
	
	bool isDrawReady(int8 shadowPass) override;
	void prepareDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass) override;
	void beginDrawAsync(int32 taskIndex) override;
	void finalizeDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass) override;
	void renderCleanup() override;

	virtual DescriptorSet::Uniforms getBaseUniforms();
	virtual DescriptorSet::Uniforms getShadowUniforms();
	virtual ID<GraphicsPipeline> createBasePipeline() = 0;
	virtual ID<GraphicsPipeline> createShadowPipeline() { return {}; }
public:
	#if GARDEN_DEBUG
	string debugResourceName = "instance";
	#endif

	/**
	 * @brief Returns mesh instance base graphics pipeline.
	 */
	ID<GraphicsPipeline> getBasePipeline();
	/**
	 * @brief Returns mesh instance shadow graphics pipeline.
	 */
	ID<GraphicsPipeline> getShadowPipeline();

	/**
	 * @brief Returns mesh base instance data size in bytes.
	 */
	virtual uint64 getBaseInstanceDataSize() = 0;
	/**
	 * @brief Returns mesh shadow instance data size in bytes.
	 */
	virtual uint64 getShadowInstanceDataSize() { return 0; }
};

} // namespace garden
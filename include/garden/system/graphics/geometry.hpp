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
#include "garden/system/graphics/mesh.hpp"

namespace garden
{

using namespace garden;
using namespace garden::graphics;

class GeometryRenderSystem;
class GeometryShadowRenderSystem;

//--------------------------------------------------------------------------------------------------
struct GeometryRenderComponent : public MeshRenderComponent
{
	Ref<Buffer> vertexBuffer = {};
	Ref<Buffer> indexBuffer = {};
	Ref<Image> baseColorMap = {};
	Ref<Image> ormMap = {};
	Ref<DescriptorSet> descriptorSet = {};
	uint32 indexCount = 0;
	uint32 indexOffset = 0;
	float4 baseColorFactor = float4(1.0f);
	float3 emissiveFactor = float3(0.0f);
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	float reflectanceFactor = 0.5f;
	GraphicsPipeline::Index indexType = {};
protected:
	uint8 _alignment1 = 0;
	uint16 _alignment2 = 0;
	friend class GeometryRenderSystem;
};

//--------------------------------------------------------------------------------------------------
class GeometryRenderSystem : public System, public IRenderSystem, public IMeshRenderSystem
{
public:
	struct InstanceData
	{
		float4x4 model = float4x4(0.0f);
		float4x4 mvp = float4x4(0.0f);
	};
protected:
	vector<vector<ID<Buffer>>> instanceBuffers = {};
	vector<GeometryRenderComponent*> dsCreateBuffer;
	mutex dsBufferLocker;
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> baseDescriptorSet = {};
	ID<DescriptorSet> defaultDescriptorSet = {};
	View<GraphicsPipeline> pipelineView = {};
	InstanceData* instanceMap = nullptr;
	int2 framebufferSize = int2(0);
	uint32 swapchainIndex = 0;

	#if GARDEN_EDITOR
	static void* editor;
	#endif

	void initialize() override;
	void terminate() override;
	
	bool isDrawReady() override;
	void prepareDraw(const float4x4& viewProj,
		ID<Framebuffer> framebuffer, uint32 drawCount) override;
	void beginDraw(int32 taskIndex) override;
	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) override;
	void finalizeDraw(const float4x4& viewProj,
		ID<Framebuffer> framebuffer, uint32 drawCount) override;
	void render() override;
	void recreateSwapchain(const SwapchainChanges& changes) override;

	virtual ID<GraphicsPipeline> createPipeline() = 0;
	virtual map<string, DescriptorSet::Uniform> getBaseUniforms();
	virtual map<string, DescriptorSet::Uniform> getDefaultUniforms();
	virtual void appendDescriptorData(Pipeline::DescriptorData* data,
		uint8& index, GeometryRenderComponent* geometryComponent) { }
	void destroyResources(GeometryRenderComponent* geometryComponent);

	friend class GeometryEditor;
public:
	ID<GraphicsPipeline> getPipeline();

	#if GARDEN_DEBUG
	ID<Entity> loadModel(const fs::path& path, uint32 sceneIndex = 0);
	#endif
};

//--------------------------------------------------------------------------------------------------
struct GeometryShadowRenderComponent : public MeshRenderComponent
{
	Ref<Buffer> vertexBuffer = {};
	Ref<Buffer> indexBuffer = {};
	uint32 indexCount = 0;
	uint32 indexOffset = 0;
	GraphicsPipeline::Index indexType = {};
protected:
	uint8 _alignment1 = 0;
	uint16 _alignment2 = 0;
	friend class GeometryShadowRenderSystem;
};

//--------------------------------------------------------------------------------------------------
class GeometryShadowRenderSystem : public System,
	public IRenderSystem, public IMeshRenderSystem
{
protected:
	ID<GraphicsPipeline> pipeline = {};
	View<GraphicsPipeline> pipelineView = {};
	int2 framebufferSize = int2(0);

	#if GARDEN_EDITOR
	static void* editor;
	#endif

	void initialize() override;
	void terminate() override;

	bool isDrawReady() override;
	void prepareDraw(const float4x4& viewProj,
		ID<Framebuffer> framebuffer, uint32 drawCount) override;
	void beginDraw(int32 taskIndex) override;
	
	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) override;

	virtual ID<GraphicsPipeline> createPipeline() = 0;
	void destroyResources(GeometryShadowRenderComponent* geometryShadowComponent);

	friend class GeometryEditor;
public:
	ID<GraphicsPipeline> getPipeline();
};

} // garden
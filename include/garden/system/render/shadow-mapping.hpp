//--------------------------------------------------------------------------------------------------
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
//
// Based on this: https://learnopengl.com/Guest-Articles/2021/CSM
//--------------------------------------------------------------------------------------------------

/*
#pragma once
#include "garden/system/render/mesh.hpp"
#include "garden/system/render/lighting.hpp"

namespace garden
{

#define SHADOW_MAP_CASCADE_COUNT 3 // TODO: use const variable instead

using namespace garden::graphics;

//--------------------------------------------------------------------------------------------------
// CSM - Cascade Shadow Mapping
class ShadowMappingRenderSystem final : public System, public IRenderSystem,
	public IShadowMeshRenderSystem, public IShadowRenderSystem
{
public:
	struct DataBuffer final
	{
		float4x4 lightSpace[SHADOW_MAP_CASCADE_COUNT];
	};
private:
	vector<ID<ImageView>> imageViews;
	vector<ID<Framebuffer>> framebuffers;
	vector<vector<ID<Buffer>>> dataBuffers;
	ID<Image> shadowMap = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	float3 farPlanes = float3(0.0f);
	int32 shadowMapSize = 2048;
	bool asyncRecording = false;

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	ShadowMappingRenderSystem(bool asyncRecording) : asyncRecording(asyncRecording) { }

	void initialize() final;
	void terminate() final;
	void render() final;
	uint32 getShadowPassCount() final;
	bool prepareShadowRender(uint32 passIndex, float4x4& viewProj,
		float3& cameraOffset, ID<Framebuffer>& framebuffer) final;
	void beginShadowRender(uint32 passIndex, MeshRenderType renderType) final;
	void endShadowRender(uint32 passIndex, MeshRenderType renderType) final;
	bool shadowRender() final;
	void recreateSwapchain(const SwapchainChanges& changes) final;
	
	friend class ecsm::Manager;
public:
	float3 splitCoefs = float3(0.05f, 0.2f, 0.75f); // 100% in sum
	float intensity = 0.75f;
	float farPlane = 100.0f;
	float minBias = 0.001f;
	float maxBias = 0.01f;
	float zCoeff = 10.0f;

	ID<Image> getShadowMap();
	ID<GraphicsPipeline> getPipeline();
	const vector<vector<ID<Buffer>>>& getDataBuffers();
	const vector<ID<Framebuffer>>& getFramebuffers();

	int32 getShadowMapSize() const noexcept { return shadowMapSize; }
	void setShadowMapSize(int32 size);
};

} // namespace garden
*/
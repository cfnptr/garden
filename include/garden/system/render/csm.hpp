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
 * @brief Cascade shadow maps rendering functions.
 * @details Based on this: https://learnopengl.com/Guest-Articles/2021/CSM
 */

#pragma once
#include "garden/system/render/mesh.hpp"

namespace garden
{

/**
 * @brief Cascade shadow maps rendering system. (CSM)
 * 
 * @details
 * CSM is a technique used in 3D graphics to improve the quality of shadows cast by directional light sources, such as 
 * sunlight, over large scenes. It divides the camera's view frustum into multiple regions (cascades) along the depth 
 * axis and creates a separate shadow map for each region. This helps maintain high-quality shadows close to the 
 * camera while optimizing shadow resolution for distant areas.
 */
class CsmRenderSystem final : public System, public Singleton<CsmRenderSystem>, public IShadowMeshRenderSystem
{
public:
	/**
	 * @brief Shadow map cascade count.
	 * @details Determines how many regions the view frustum is divided into.
	 */
	static constexpr uint8 cascadeCount = 3;

	struct ShadowData final
	{
		float4 farPlanesIntens;
		float4x4 lightSpace[cascadeCount];
	};
private:
	f32x4 farPlanes = f32x4::zero;
	vector<ID<ImageView>> imageViews;
	vector<ID<Framebuffer>> framebuffers;
	DescriptorSetBuffers dataBuffers;
	ID<Image> shadowMap = {};
	ID<GraphicsPipeline> pipeline = {};
	ID<DescriptorSet> descriptorSet = {};
	uint32 shadowMapSize = 2048;

	/**
	 * @brief Creates a new cascade shadow maps rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	CsmRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys cascade shadow maps rendering system instance.
	 */
	~CsmRenderSystem() final;

	void init();
	void deinit();
	void shadowRender();
	void gBufferRecreate();

	uint32 getShadowPassCount() final;
	bool prepareShadowRender(uint32 passIndex, f32x4x4& viewProj, f32x4& cameraOffset) final;
	void beginShadowRender(uint32 passIndex, MeshRenderType renderType) final;
	void endShadowRender(uint32 passIndex, MeshRenderType renderType) final;
	
	friend class ecsm::Manager;
public:
	float2 cascadeSplits = float2(0.05f, 0.20f);
	float distance = 100.0f;
	float biasConstantFactor = -1.25f;
	float biasSlopeFactor = -1.75f;
	float zCoeff = 10.0f;

	/**
	 * @brief Returns frustum far planes for each shadow map cascade.
	 */
	f32x4 getFarPlanes() const noexcept { return farPlanes; }

	/**
	 * @brief Returns shadow map size in pixels along one axis.
	 */
	uint32 getShadowMapSize() const noexcept { return shadowMapSize; }
	/**
	 * @brief Sets shadow map size in pixels along one axis.
	 */
	void setShadowMapSize(uint32 size);

	/**
	 * @brief Returns cascade shadow mapping graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();
	/**
	 * @brief Returns cascade shadow map buffer.
	 */
	ID<Image> getShadowMap();
	/**
	 * @brief Returns cascade shadow mapping data buffers.
	 */
	const DescriptorSetBuffers& getDataBuffers();
	/**
	 * @brief Returns cascade shadow mapping framebuffers
	 */
	const vector<ID<Framebuffer>>& getFramebuffers();	
};

} // namespace garden
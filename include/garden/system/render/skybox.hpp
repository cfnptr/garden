// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
 * @brief Skybox rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief Skybox rendering data container.
 */
struct SkyboxRenderComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< Skybox cubemap texture. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Skybox descriptor set. */
};

/**
 * @brief Skybox rendering system.
 *
 * @details
 * Skybox is a method of creating the illusion of a vast, distant background by enclosing the game world within a large, 
 * textured cube that surrounds the camera. The textures applied to the interior faces of this box render "behind" all 
 * other objects in the scene, effectively simulating the sky, horizon, and distant environmental features like 
 * mountains or stars.
 */
class SkyboxRenderSystem final : public ComponentSystem<
	SkyboxRenderComponent, false>, public Singleton<SkyboxRenderSystem>
{
public:
	struct PushConstants final
	{
		float4x4 viewProj;
	};
private:
	ID<GraphicsPipeline> pipeline = {};

	/**
	 * @brief Creates a new skybox rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	SkyboxRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys skybox rendering system instance.
	 */
	~SkyboxRenderSystem() final;

	void init();
	void deinit();
	void imageLoaded();
	void depthHdrRender();

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;

	friend class ecsm::Manager;
public:
	bool isEnabled = true; /**< Is skybox rendering enabled. */

	/**
	 * @brief Returns skybox graphics pipeline.
	 */
	ID<GraphicsPipeline> getPipeline();

	/**
	 * @brief Creates shared skybox descriptor set.
	 * 
	 * @param path skybox resource path
	 * @param cubemap skybox cubemap instance
	 */
	Ref<DescriptorSet> createSharedDS(string_view path, ID<Image> cubemap);
};

} // namespace garden
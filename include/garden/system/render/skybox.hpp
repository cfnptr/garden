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
 * @brief Skybox rendering functions.
 */

#pragma once
#include "garden/system/render/deferred.hpp"

namespace garden
{

/**
 * @brief Skybox rendering data container.
 */
struct SkyboxRenderComponent final : public Component
{
	Ref<Image> cubemap = {};               /**< Skybox cubemap texture. */
	Ref<DescriptorSet> descriptorSet = {}; /**< Skybox descriptor set. */

	bool destroy();
};

/**
 * @brief Skybox rendering system.
 */
class SkyboxRenderSystem final : public ComponentSystem<SkyboxRenderComponent>, public Singleton<SkyboxRenderSystem>
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
	void translucentRender();

	const string& getComponentName() const final;
	friend class ecsm::Manager;
public:
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
	Ref<DescriptorSet> createSharedDS(const string& path, ID<Image> cubemap);
};

} // namespace garden
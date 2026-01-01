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
 * @brief Model level of detail (LOD) rendering functions.
 */

#pragma once
#include "garden/system/graphics.hpp"

namespace garden
{

/**
 * @brief 3D model LOD rendering data conatiner. (Level of detail)
 */
struct ModelLOD final
{
	Ref<Buffer> vertexBuffer = {}; /**< Buffer containing 3D model vertex data. */
	Ref<Buffer> indexBuffer = {};  /**< Buffer containing 3D model indices. */
};

/**
 * @brief 3D model rendering data container.
 */
struct ModelRenderComponent final : public Component
{
	vector<ModelLOD> levels;
};

/**
 * @brief 3D model rendering system.
 */
class ModelRenderSystem final : public ComponentSystem<
	ModelRenderComponent, false>, public Singleton<ModelRenderSystem>
{
	/**
	 * @brief Creates a new 3D model rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	ModelRenderSystem(bool setSingleton = true);
	/**
	 * @brief Destroys 3D model rendering system instance.
	 */
	~ModelRenderSystem() final;

	void init();
	void deinit();

	void resetComponent(View<Component> component, bool full) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	string_view getComponentName() const final;

	friend class ecsm::Manager;
};

} // namespace garden
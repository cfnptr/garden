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
 * @brief Opaque model rendering functions.
 */

#pragma once
#include "garden/system/render/model.hpp"

namespace garden
{

/**
 * @brief Opaque model rendering data container.
 */
struct OpaqueModelComponent final : public ModelRenderComponent { };
/**
 * @brief Opaque model animation frame container.
 */
struct OpaqueModelFrame final : public ModelAnimationFrame { };

/**
 * @brief Opaque model rendering system.
 */
class OpaqueModelSystem final : public ModelRenderCompSystem<
	OpaqueModelComponent, OpaqueModelFrame, false, false>, public Singleton<OpaqueModelSystem>
{
	/**
	 * @brief Creates a new opaque model rendering system instance.
	 * 
	 * @param useNormalMapping load and use normal map textures
	 * @param setSingleton set system singleton instance
	 */
	OpaqueModelSystem(bool useNormalMapping = true, bool setSingleton = true);
	/**
	 * @brief Destroys opaque model rendering system instance.
	 */
	~OpaqueModelSystem() final;

	const string& getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
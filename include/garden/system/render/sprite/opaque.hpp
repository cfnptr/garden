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
 * @brief Opaque sprite rendering functions.
 */

#pragma once
#include "garden/system/render/sprite.hpp"

namespace garden
{

/**
 * @brief Opaque sprite rendering data container.
 */
struct OpaqueSpriteComponent final : public SpriteRenderComponent { };
/**
 * @brief Opaque sprite animation frame container.
 */
struct OpaqueSpriteFrame final : public SpriteAnimFrame { };

/**
 * @brief Opaque sprite rendering system.
 */
class OpaqueSpriteSystem final : public SpriteCompAnimSystem<
	OpaqueSpriteComponent, OpaqueSpriteFrame, false, false>, public Singleton<OpaqueSpriteSystem>
{
	/**
	 * @brief Creates a new opaque sprite rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	OpaqueSpriteSystem(bool setSingleton = true);
	/**
	 * @brief Destroys opaque sprite rendering system instance.
	 */
	~OpaqueSpriteSystem() final;

	string_view getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
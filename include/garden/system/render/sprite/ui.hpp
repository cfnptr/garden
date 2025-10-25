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
 * @brief User interface sprite rendering functions. (UI)
 */

#pragma once
#include "garden/system/render/sprite.hpp"

namespace garden
{

/**
 * @brief User interface sprite rendering data container. (UI)
 */
struct UiSpriteComponent final : public SpriteRenderComponent { };
/**
 * @brief User interface sprite animation frame container. (UI)
 */
struct UiSpriteFrame final : public SpriteAnimFrame { };

/**
 * @brief User interface sprite rendering system. (UI)
 */
class UiSpriteSystem final : public SpriteRenderCompSystem<
	UiSpriteComponent, UiSpriteFrame, false, false>, public Singleton<UiSpriteSystem>
{
	/**
	 * @brief Creates a new user interface sprite rendering system instance. (UI)
	 * @param setSingleton set system singleton instance
	 */
	UiSpriteSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface sprite rendering system instance. (UI)
	 */
	~UiSpriteSystem() final;

	string_view getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
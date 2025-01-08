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
 * @brief Translucent sprite rendering functions.
 */

#pragma once
#include "garden/system/render/sprite.hpp"

namespace garden
{

/**
 * @brief Translucent sprite rendering data container.
 */
struct TransSpriteComponent final : public SpriteRenderComponent { };
/**
 * @brief Translucent sprite animation frame container.
 */
struct TransSpriteFrame final : public SpriteAnimationFrame { };

/**
 * @brief Translucent sprite rendering system.
 */
class TransSpriteSystem final : public SpriteRenderCompSystem<
	TransSpriteComponent, TransSpriteFrame, false, false>, public Singleton<TransSpriteSystem>
{
	/**
	 * @brief Creates a new translucent sprite rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 * @param setSingleton set system singleton instance
	 */
	TransSpriteSystem(bool useDeferredBuffer = false, bool useLinearFilter = true, bool setSingleton = true);
	/**
	 * @brief Destroys opaque translucent rendering system instance.
	 */
	~TransSpriteSystem() final;

	const string& getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
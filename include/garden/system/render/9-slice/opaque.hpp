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
 * @brief Opaque 9-slice sprite rendering functions.
 */

#pragma once
#include "garden/system/render/9-slice.hpp"

namespace garden
{

/**
 * @brief Opaque 9-slice sprite rendering data container.
 */
struct Opaque9SliceComponent final : public NineSliceRenderComponent { };
/**
 * @brief Opaque 9-slice sprite animation frame container.
 */
struct Opaque9SliceFrame final : public NineSliceAnimationFrame { };

/**
 * @brief Opaque 9-slice sprite rendering system.
 */
class Opaque9SliceSystem final : public NineSliceRenderCompSystem<
	Opaque9SliceComponent, Opaque9SliceFrame, false, false>, public Singleton<Opaque9SliceSystem>
{
	/**
	 * @brief Creates a new opaque 9-slice rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 * @param setSingleton set system singleton instance
	 */
	Opaque9SliceSystem(bool useDeferredBuffer = false, bool useLinearFilter = true, bool setSingleton = true);
	/**
	 * @brief Destroys opaque 9-slice rendering system instance.
	 */
	~Opaque9SliceSystem() final;

	const string& getComponentName() const final;
	friend class ecsm::Manager;
};

} // namespace garden
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
 * @brief Translucent 9-slice sprite rendering functions.
 */

#pragma once
#include "garden/system/render/9-slice.hpp"

namespace garden
{

struct Translucent9SliceComponent final : public NineSliceRenderComponent { };
struct Translucent9SliceFrame final : public NineSliceAnimationFrame { };

class Translucent9SliceSystem final : public NineSliceRenderCompSystem<
	Translucent9SliceComponent, Translucent9SliceFrame, false, false>, public Singleton<Translucent9SliceSystem>
{
	/**
	 * @brief Creates a new translucent 9-slice rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 * @param setSingleton set system singleton instance
	 */
	Translucent9SliceSystem(bool useDeferredBuffer = false, bool useLinearFilter = true, bool setSingleton = true);
	/**
	 * @brief Destroys translucent 9-slice rendering system instance.
	 */
	~Translucent9SliceSystem() final;

	const string& getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
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
 * @brief Translucent 9-slice sprite rendering functions.
 */

#pragma once
#include "garden/system/render/9-slice.hpp"

namespace garden
{

/**
 * @brief Translucent 9-slice sprite rendering data container.
 */
struct Trans9SliceComponent final : public NineSliceComponent { };
/**
 * @brief Translucent 9-slice sprite animation frame container.
 */
struct Trans9SliceFrame final : public NineSliceFrame { };

/**
 * @brief Translucent 9-slice sprite rendering system.
 */
class Trans9SliceSystem final : public NineSliceCompAnimSystem<
	Trans9SliceComponent, Trans9SliceFrame, false, false>, public Singleton<Trans9SliceSystem>
{
	/**
	 * @brief Creates a new translucent 9-slice rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	Trans9SliceSystem(bool setSingleton = true);
	/**
	 * @brief Destroys translucent 9-slice rendering system instance.
	 */
	~Trans9SliceSystem() final;

	string_view getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;
	
	friend class ecsm::Manager;
};

} // namespace garden
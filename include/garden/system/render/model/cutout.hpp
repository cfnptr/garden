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
 * @brief Cutout 3D model rendering functions.
 */

#pragma once
#include "garden/system/render/model.hpp"

namespace garden
{

/**
 * @brief Cutout 3D model rendering data container.
 */
struct CutoutModelComponent final : public ModelRenderComponent { };
/**
 * @brief Cutout 3D model animation frame container.
 */
struct CutoutModelFrame final : public ModelAnimFrame { };

/**
 * @brief Cutout 3D model rendering system.
 */
class CutoutModelSystem final : public ModelCompAnimSystem<
	CutoutModelComponent, CutoutModelFrame, false, false>, public Singleton<CutoutModelSystem>
{
	/**
	 * @brief Creates a new cutout 3D model rendering system instance.
	 * @param setSingleton set system singleton instance
	 */
	CutoutModelSystem(bool setSingleton = true);

	string_view getComponentName() const override;
	MeshRenderType getMeshRenderType() const override;
	
	friend class ecsm::Manager;
};

} // namespace garden
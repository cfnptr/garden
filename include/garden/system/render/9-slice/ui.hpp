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
 * @brief User interface 9-slice sprite rendering functions. (UI)
 */

#pragma once
#include "garden/system/render/9-slice.hpp"
#include "garden/system/ui/scissor.hpp"

namespace garden
{

/**
 * @brief User interface 9-slice sprite rendering data container. (UI)
 */
struct Ui9SliceComponent final : public NineSliceComponent { };
/**
 * @brief User interface 9-slice sprite animation frame container. (UI)
 */
struct Ui9SliceFrame final : public NineSliceFrame { };

/**
 * @brief User interface 9-slice sprite rendering system. (UI)
 */
class Ui9SliceSystem final : public NineSliceCompAnimSystem<
	Ui9SliceComponent, Ui9SliceFrame, false, false>, public Singleton<Ui9SliceSystem>
{
	UiScissorSystem* uiScissorSystem = nullptr;

	/**
	 * @brief Creates a new user interface 9-slice rendering system instance. (UI)
	 * @param setSingleton set system singleton instance
	 */
	Ui9SliceSystem(bool setSingleton = true);
	/**
	 * @brief Destroys user interface 9-slice rendering system instance. (UI)
	 */
	~Ui9SliceSystem() final;

	string_view getComponentName() const final;
	MeshRenderType getMeshRenderType() const final;

	void beginDrawAsync(int32 taskIndex) final;
	void prepareDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass) final;
	void drawAsync(MeshRenderComponent* meshRenderView, const f32x4x4& viewProj,
		const f32x4x4& model, uint32 drawIndex, int32 taskIndex) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
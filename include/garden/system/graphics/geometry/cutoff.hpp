//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once
#include "garden/system/graphics/geometry.hpp"

namespace garden
{

using namespace garden;
using namespace garden::graphics;

class CutoffRenderSystem;

//--------------------------------------------------------------------------------------------------
struct CutoffRenderComponent final : public GeometryRenderComponent
{
	float alphaCutoff = 0.0f;
	friend class CutoffRenderSystem;
};

//--------------------------------------------------------------------------------------------------
class CutoffRenderSystem final : public GeometryRenderSystem
{
	LinearPool<CutoffRenderComponent, false> components;

	void initialize() final;

	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	MeshRenderType getMeshRenderType() const final;
	const LinearPool<MeshRenderComponent>& getMeshComponentPool() const final;
	psize getMeshComponentSize() const final;
	ID<GraphicsPipeline> createPipeline() final;
	
	friend class ecsm::Manager;
};

} // garden
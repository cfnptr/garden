//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/graphics/deferred.hpp"

namespace garden
{

using namespace garden;
using namespace garden::graphics;
class SkyboxRenderSystem;

//--------------------------------------------------------------------------------------------------
struct SkyboxRenderComponent final : public Component
{
	Ref<Image> cubemap = {};
	Ref<DescriptorSet> descriptorSet = {};
};

//--------------------------------------------------------------------------------------------------
class SkyboxRenderSystem final : public System,
	public IRenderSystem, public IDeferredRenderSystem
{
	LinearPool<SkyboxRenderComponent, false> components;
	ID<Buffer> fullCubeVertices = {};
	ID<GraphicsPipeline> pipeline = {};

	#if GARDEN_EDITOR
	void* editor = nullptr;
	#endif

	void initialize() final;
	void terminate() final;
	void hdrRender() final;

	type_index getComponentType() const final;
	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	friend class ecsm::Manager;
public:
	ID<GraphicsPipeline> getPipeline();
	ID<DescriptorSet> createDescriptorSet(ID<Image> cubemap);
};

} // garden
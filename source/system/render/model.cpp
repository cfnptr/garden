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

#include "garden/system/render/model.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

ModelRenderSystem::ModelRenderSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Init", ModelRenderSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ModelRenderSystem::deinit);
}
ModelRenderSystem::~ModelRenderSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ModelRenderSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ModelRenderSystem::deinit);
	}

	unsetSingleton();
}

void ModelRenderSystem::resetComponent(View<Component> component, bool full)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	auto componentView = View<ModelRenderComponent>(component);

	if (!componentView->levels.empty())
	{
		for (auto& lod : componentView->levels)
		{
			resourceSystem->destroyShared(lod.vertexBuffer);
			resourceSystem->destroyShared(lod.indexBuffer);
		}
	}

	if (full)
		**componentView = {};
}
void ModelRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<ModelRenderComponent>(destination);
	const auto sourceView = View<ModelRenderComponent>(source);
	destinationView->levels = sourceView->levels;
}
string_view ModelRenderSystem::getComponentName() const
{
	return "Model";
}

//**********************************************************************************************************************
void ModelRenderSystem::init()
{
	
}
void ModelRenderSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		
	}
}
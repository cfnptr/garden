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

#include "garden/system/demo.hpp"
#include "garden/system/graphics/skybox.hpp"
#include "garden/system/graphics/geometry.hpp"
#include "garden/system/graphics/lighting.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
void DemoSystem::initialize()
{
	auto manager = getManager();
	auto graphicsSystem = manager->get<GraphicsSystem>();
	auto lightingSystem = manager->get<LightingRenderSystem>();

	auto camera = manager->createEntity();
	auto transformComponent = manager->add<TransformComponent>(camera);
	transformComponent->position = float3(0.0f, 0.0f, -2.0f);
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformComponent->name = "Camera";
	#endif
	graphicsSystem->camera = camera;
	manager->add<CameraComponent>(camera);
	manager->add<DoNotDestroyComponent>(camera);

	auto sun = manager->createEntity();
	transformComponent = manager->add<TransformComponent>(sun);
	transformComponent->rotation = lookAtQuat(normalize(-float3(1.0f, 6.0f, 2.0f)));
	#if GARDEN_DEBUG || GARDEN_EDITOR
	transformComponent->name = "Sun";
	#endif
	graphicsSystem->directionalLight = sun;
	manager->add<DoNotDestroyComponent>(sun);

	auto lightingComponent = manager->add<LightingRenderComponent>(camera);
	lightingSystem->loadCubemap("cubemaps/pure-sky", lightingComponent->cubemap,
		lightingComponent->sh, lightingComponent->specular);
	auto skyboxComponent = manager->add<SkyboxRenderComponent>(camera);
	skyboxComponent->cubemap = lightingComponent->cubemap;
}

//--------------------------------------------------------------------------------------------------
void DemoSystem::update()
{
	if (!isLoaded)
	{
		auto geometrySystem = getManager()->get<OpaqueRenderSystem>();
		// TODO:
		// geometrySystem->loadModel("sponza/NewSponza_Main_glTF_002");
		// geometrySystem->loadModel("EnvironmentTest/EnvironmentTest");
		// geometrySystem->loadModel("AlphaBlendModeTest/AlphaBlendModeTest");
		isLoaded = true;
	}
}
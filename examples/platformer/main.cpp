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

#include "garden/main.hpp"
#include "garden/system/log.hpp"
#include "garden/system/link.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/physics.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/2d-controller.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/render/sprite/opaque.hpp"
#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/sprite/translucent.hpp"
#include "garden/system/render/9-slice/opaque.hpp"
// #include "garden/system/render/9-slice/cutout.hpp"
// #include "garden/system/render/9-slice/translucent.hpp"
#include "platformer/defines.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/log.hpp"
#include "garden/editor/system/ecs.hpp"
#include "garden/editor/system/link.hpp"
#include "garden/editor/system/camera.hpp"
#include "garden/editor/system/physics.hpp"
#include "garden/editor/system/graphics.hpp"
#include "garden/editor/system/transform.hpp"
#include "garden/editor/system/hierarchy.hpp"
#include "garden/editor/system/animation.hpp"
#include "garden/editor/system/render/sprite.hpp"
#include "garden/editor/system/render/9-slice.hpp"
#include "garden/editor/system/render/mesh-gizmos.hpp"
#include "garden/editor/system/render/mesh-selector.hpp"
#include "garden/editor/system/render/gpu-resource.hpp"
#include "garden/editor/system/render/infinite-grid.hpp"
#endif

using namespace ecsm;
using namespace garden;
using namespace platformer;

static void loadWindowData()
{
	auto graphicsSystem = GraphicsSystem::getInstance();
	graphicsSystem->setWindowTitle("Platformer");

	#if GARDEN_OS_WINDOWS
	graphicsSystem->setWindowIcon(
	{
		"windows/icon96x96", "windows/icon64x64",
		"windows/icon32x32", "windows/icon16x16"
	});
	#endif
}

void entryPoint()
{
	auto manager = new Manager();
	createAppSystem();
	manager->createSystem<DoNotDestroySystem>();
	manager->createSystem<DoNotDuplicateSystem>();
	manager->createSystem<DoNotSerializeSystem>();
	manager->createSystem<LogSystem>();
	manager->createSystem<SettingsSystem>();
	manager->createSystem<ResourceSystem>();
	manager->createSystem<LinkSystem>();
	manager->createSystem<AnimationSystem>();
	manager->createSystem<CameraSystem>();
	manager->createSystem<TransformSystem>();
	manager->createSystem<BakedTransformSystem>();
	manager->createSystem<PhysicsSystem>();
	manager->createSystem<InputSystem>();
	manager->createSystem<GraphicsSystem>();
	manager->createSystem<ForwardRenderSystem>();
	manager->createSystem<MeshRenderSystem>();
	manager->createSystem<OpaqueSpriteSystem>(false, false);
	manager->createSystem<CutoutSpriteSystem>(false, false);
	manager->createSystem<TranslucentSpriteSystem>(false, false);
	manager->createSystem<Opaque9SliceSystem>(false, false);
	// manager->createSystem<Cutout9SliceSystem>(false, false);
	// manager->createSystem<Translucent9SliceSystem>(false, false);
	manager->createSystem<Controller2DSystem>();
	manager->createSystem<ThreadSystem>();

	#if GARDEN_EDITOR
	manager->createSystem<EditorRenderSystem>();
	manager->createSystem<HierarchyEditorSystem>();
	manager->createSystem<EcsEditorSystem>();
	manager->createSystem<LogEditorSystem>();
	manager->createSystem<LinkEditorSystem>();
	manager->createSystem<AnimationEditorSystem>();
	manager->createSystem<CameraEditorSystem>();
	manager->createSystem<TransformEditorSystem>();
	manager->createSystem<PhysicsEditorSystem>();
	manager->createSystem<GraphicsEditorSystem>();
	manager->createSystem<GpuResourceEditorSystem>();
	manager->createSystem<InfiniteGridEditorSystem>();
	manager->createSystem<MeshSelectorEditorSystem>();
	manager->createSystem<MeshGizmosEditorSystem>();
	// manager->createSystem<LightingRenderEditorSystem>();
	manager->createSystem<SpriteRenderEditorSystem>();
	manager->createSystem<NineSliceRenderEditorSystem>();
	#endif

	manager->initialize();
	loadWindowData();
	manager->start();
	delete manager;
}

GARDEN_DECLARE_MAIN(entryPoint)
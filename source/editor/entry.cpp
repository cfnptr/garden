// Copyright 2024-2026 Nikita Fediuchin. All rights reserved.
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

#include "garden-editor/app-defines.hpp"
#include "garden/main.hpp"
#include "garden/system/ui.hpp"
#include "garden/system/log.hpp"
#include "garden/system/link.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/camera.hpp"
#include "garden/system/spawner.hpp"
#include "garden/system/settings.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/character.hpp"
#include "garden/system/steam-api.hpp"
#include "garden/system/file-watcher.hpp"
#include "garden/system/controller/fpv.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/render/oit.hpp"
#include "garden/system/render/csm.hpp"
#include "garden/system/render/mesh.hpp"
#include "garden/system/render/smaa.hpp"
#include "garden/system/render/hbao.hpp"
#include "garden/system/render/dlss.hpp"
#include "garden/system/render/blur.hpp"
#include "garden/system/render/bloom.hpp"
#include "garden/system/render/clouds.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/atmosphere.hpp"
#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/pbr-lighting.hpp"
#include "garden/system/render/tone-mapping.hpp"
#include "garden/system/render/auto-exposure.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/imgui.hpp"
#include "garden/editor/system/log.hpp"
#include "garden/editor/system/ecs.hpp"
#include "garden/editor/system/link.hpp"
#include "garden/editor/system/camera.hpp"
#include "garden/editor/system/spawner.hpp"
#include "garden/editor/system/physics.hpp"
#include "garden/editor/system/graphics.hpp"
#include "garden/editor/system/transform.hpp"
#include "garden/editor/system/hierarchy.hpp"
#include "garden/editor/system/animation.hpp"
#include "garden/editor/system/render/csm.hpp"
#include "garden/editor/system/render/smaa.hpp"
#include "garden/editor/system/render/dlss.hpp"
#include "garden/editor/system/render/hbao.hpp"
#include "garden/editor/system/render/bloom.hpp"
#include "garden/editor/system/render/clouds.hpp"
#include "garden/editor/system/render/sprite.hpp"
#include "garden/editor/system/render/9-slice.hpp"
#include "garden/editor/system/render/deferred.hpp"
#include "garden/editor/system/render/atmosphere.hpp"
#include "garden/editor/system/render/pbr-lighting.hpp"
#include "garden/editor/system/render/tone-mapping.hpp"
#include "garden/editor/system/render/auto-exposure.hpp"
#include "garden/editor/system/render/mesh-gizmos.hpp"
#include "garden/editor/system/render/mesh-selector.hpp"
#include "garden/editor/system/render/gpu-resource.hpp"
#include "garden/editor/system/render/infinite-grid.hpp"
#endif

using namespace garden;
using namespace garden::editor;

static void entryPoint()
{
	ToneMappingSystem::Options toneMappingOptions;
	toneMappingOptions.useBloomBuffer = true;

	auto manager = new Manager();
	createAppSystem(manager);

	manager->createSystem<ThreadSystem>();
	#if GARDEN_STEAMWORKS_SDK
	manager->createSystem<SteamApiSystem>();
	#endif
	manager->createSystem<DoNotDestroySystem>();
	manager->createSystem<DoNotDuplicateSystem>();
	manager->createSystem<DoNotSerializeSystem>();
	manager->createSystem<StaticTransformSystem>();
	manager->createSystem<LogSystem>();
	manager->createSystem<SettingsSystem>();
	manager->createSystem<ResourceSystem>();
	// manager->createSystem<LocaleSystem>();
	manager->createSystem<LinkSystem>();
	manager->createSystem<CameraSystem>();
	manager->createSystem<TransformSystem>();
	manager->createSystem<InputSystem>();
	manager->createSystem<FpvControllerSystem>();
	manager->createSystem<SpawnerSystem>();
	manager->createSystem<AnimationSystem>();
	manager->createSystem<TextSystem>();
	manager->createSystem<UiTransformSystem>();
	manager->createSystem<UiScissorSystem>();
	manager->createSystem<UiTriggerSystem>();
	manager->createSystem<UiLabelSystem>();
	manager->createSystem<UiButtonSystem>();
	manager->createSystem<UiCheckboxSystem>();
	manager->createSystem<UiInputSystem>();
	manager->createSystem<PhysicsSystem>();
	manager->createSystem<CharacterSystem>();
	#if GARDEN_EDITOR
	manager->createSystem<FileWatcherSystem>();
	manager->createSystem<ImGuiRenderSystem>();
	#endif
	manager->createSystem<GraphicsSystem>();
	manager->createSystem<GpuProcessSystem>();
	manager->createSystem<DeferredRenderSystem>();
	manager->createSystem<HizRenderSystem>();
	manager->createSystem<PbrLightingSystem>();
	manager->createSystem<AtmosphereRenderSystem>();
	manager->createSystem<CloudsRenderSystem>();
	manager->createSystem<MeshRenderSystem>();
	manager->createSystem<ModelStoreSystem>();
	// manager->createSystem<OpaqueSpriteSystem>();
	// manager->createSystem<CutoutSpriteSystem>();
	// manager->createSystem<Opaque9SliceSystem>();
	// manager->createSystem<Cutout9SliceSystem>();
	manager->createSystem<CsmRenderSystem>();
	manager->createSystem<HbaoRenderSystem>();
	#if GARDEN_NVIDIA_DLSS
	manager->createSystem<DlssRenderSystem>();
	#endif
	manager->createSystem<OitRenderSystem>();
	manager->createSystem<BloomRenderSystem>();
	manager->createSystem<ToneMappingSystem>(toneMappingOptions);
	manager->createSystem<AutoExposureSystem>();
	manager->createSystem<BlurRenderSystem>();
	manager->createSystem<SmaaRenderSystem>();
	// manager->createSystem<NetworkSystem>();

		#if GARDEN_EDITOR
	manager->createSystem<EditorRenderSystem>();
	manager->createSystem<HierarchyEditorSystem>();
	manager->createSystem<EcsEditorSystem>();
	manager->createSystem<LogEditorSystem>();
	manager->createSystem<LinkEditorSystem>();
	manager->createSystem<CameraEditorSystem>();
	manager->createSystem<TransformEditorSystem>();
	manager->createSystem<AnimationEditorSystem>();
	manager->createSystem<UiTransformEditorSystem>();
	manager->createSystem<UiScissorEditorSystem>();
	manager->createSystem<UiTriggerEditorSystem>();
	manager->createSystem<UiLabelEditorSystem>();
	manager->createSystem<UiButtonEditorSystem>();
	manager->createSystem<UiCheckboxEditorSystem>();
	manager->createSystem<UiInputEditorSystem>();
	manager->createSystem<PhysicsEditorSystem>();
	manager->createSystem<GraphicsEditorSystem>();
	manager->createSystem<GpuResourceEditorSystem>();
	manager->createSystem<InfiniteGridEditorSystem>();
	manager->createSystem<MeshSelectorEditorSystem>();
	manager->createSystem<MeshGizmosEditorSystem>();
	manager->createSystem<DeferredRenderEditorSystem>();
	manager->createSystem<AtmosphereEditorSystem>();
	manager->createSystem<CloudsEditorSystem>();
	manager->createSystem<SpriteRenderEditorSystem>();
	manager->createSystem<NineSliceEditorSystem>();
	manager->createSystem<PbrLightingEditorSystem>();
	manager->createSystem<CsmRenderEditorSystem>();
	manager->createSystem<HbaoRenderEditorSystem>();
	#if GARDEN_NVIDIA_DLSS
	manager->createSystem<DlssRenderEditorSystem>();
	#endif
	manager->createSystem<BloomRenderEditorSystem>();
	manager->createSystem<ToneMappingEditorSystem>();
	manager->createSystem<AutoExposureEditorSystem>();
	manager->createSystem<SmaaRenderEditorSystem>();
	// manager->createSystem<NetworkEditorSystem>();
	#endif

	manager->initialize();
	InputSystem::startRenderThread();
	manager->terminate();
}

GARDEN_DECLARE_MAIN(entryPoint)
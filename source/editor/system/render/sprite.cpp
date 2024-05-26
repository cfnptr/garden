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

#include "garden/editor/system/render/sprite.hpp"

#if GARDEN_EDITOR
#include "garden/system/app-info.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/render/sprite/cutout.hpp"

using namespace garden;

SpriteRenderEditorSystem::SpriteRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", SpriteRenderEditorSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
}
SpriteRenderEditorSystem::~SpriteRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", SpriteRenderEditorSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
	}
}

void SpriteRenderEditorSystem::init()
{
	auto manager = Manager::getInstance();
	GARDEN_ASSERT(manager->has<EditorRenderSystem>());

	auto editorSystem = EditorRenderSystem::getInstance();
	if (manager->has<CutoutSpriteSystem>())
	{
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
}
void SpriteRenderEditorSystem::deinit()
{
	EditorRenderSystem::getInstance()->tryUnregisterEntityInspector<CutoutSpriteComponent>();
}

//**********************************************************************************************************************
static void renderSpriteComponent(SpriteRenderComponent* spriteComponent)
{
	ImGui::InputText("Path", &spriteComponent->path, ImGuiInputTextFlags_ReadOnly); ImGui::SameLine();
	if (ImGui::Button("Select"))
	{
		auto entity = spriteComponent->getEntity();
		auto appInfoSystem = AppInfoSystem::getInstance();
		auto editorSystem = EditorRenderSystem::getInstance();
		static const set<string> extensions = { ".webp", ".png", ".jpg", ".jpeg", ".exr", ".hdr" };

		editorSystem->openFileSelector([entity](const fs::path& selectedFile)
		{
			if (EditorRenderSystem::getInstance()->selectedEntity == entity)
			{
				auto manager = Manager::getInstance();
				auto path = selectedFile;
				path.replace_extension();

				auto cutoutComponent = manager->tryGet<CutoutSpriteComponent>(entity);
				if (cutoutComponent)
				{
					auto graphicsSystem = GraphicsSystem::getInstance();
					if (cutoutComponent->colorMap.getRefCount() == 1)
						graphicsSystem->destroy(cutoutComponent->colorMap);
					if (cutoutComponent->descriptorSet.getRefCount() == 1)
						graphicsSystem->destroy(cutoutComponent->descriptorSet);

					cutoutComponent->path = path.generic_string();
					cutoutComponent->colorMap = ResourceSystem::getInstance()->loadImage(
						path, Image::Bind::TransferDst | Image::Bind::Sampled);
					cutoutComponent->descriptorSet = {};
				}
			}
		},
		appInfoSystem->getResourcesPath() / "images", extensions);
	}

	auto& aabb = spriteComponent->aabb;
	ImGui::Checkbox("Enabled", &spriteComponent->isEnabled);
	ImGui::DragFloat3("Min AABB", (float*)&aabb.getMin(), 0.01f);
	ImGui::DragFloat3("Max AABB", (float*)&aabb.getMax(), 0.01f);
	ImGui::SliderFloat4("Color Factor", (float*)&spriteComponent->colorFactor, 0.0f, 1.0f);
}

void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (isOpened)
	{
		auto cutoutComponent = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		renderSpriteComponent(*cutoutComponent);
		ImGui::SliderFloat("Alpha Cutoff", &cutoutComponent->alphaCutoff, 0.0f, 1.0f);
	}
}
#endif
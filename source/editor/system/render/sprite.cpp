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

#include "garden/editor/system/render/sprite.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/render/sprite/ui.hpp"
#include "garden/system/render/sprite/opaque.hpp"
#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/sprite/translucent.hpp"

using namespace garden;

SpriteRenderEditorSystem::SpriteRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", SpriteRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
}
SpriteRenderEditorSystem::~SpriteRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", SpriteRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", SpriteRenderEditorSystem::deinit);
	}
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::init()
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	if (OpaqueSpriteSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<OpaqueSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	if (CutoutSpriteSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (TransSpriteSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<TransSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTransEntityInspector(entity, isOpened);
		});
	}
	if (UiSpriteSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<UiSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onUiEntityInspector(entity, isOpened);
		});
	}
}
void SpriteRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto editorSystem = EditorRenderSystem::Instance::get();
		editorSystem->tryUnregisterEntityInspector<OpaqueSpriteComponent>();
		editorSystem->tryUnregisterEntityInspector<CutoutSpriteComponent>();
		editorSystem->tryUnregisterEntityInspector<TransSpriteComponent>();
		editorSystem->tryUnregisterEntityInspector<UiSpriteComponent>();
	}
}

//**********************************************************************************************************************
template<class S>
static void renderSpriteTooltip(ID<Entity> entity)
{
	if (ImGui::BeginItemTooltip())
	{
		auto spriteView = S::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", spriteView->isEnabled ? "true" : "false",
			spriteView->colorMapPath.empty() ? "<null>" : spriteView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
}

void SpriteRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<OpaqueSpriteSystem>(entity);
	if (isOpened)
	{
		auto opaqueSpriteView = OpaqueSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*opaqueSpriteView, typeid(OpaqueSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<CutoutSpriteSystem>(entity);
	if (isOpened)
	{
		auto cutoutSpriteView = CutoutSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*cutoutSpriteView, typeid(CutoutSpriteComponent));

		ImGui::SliderFloat("Alpha Cutoff", &cutoutSpriteView->alphaCutoff, 0.0f, 1.0f);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cutoutSpriteView->alphaCutoff = 0.5f;
			ImGui::EndPopup();
		}
	}
}
void SpriteRenderEditorSystem::onTransEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<TransSpriteSystem>(entity);
	if (isOpened)
	{
		auto transSpriteView = TransSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*transSpriteView, typeid(TransSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onUiEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<UiSpriteSystem>(entity);
	if (isOpened)
	{
		auto uiSpriteView = UiSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*uiSpriteView, typeid(UiSpriteComponent));
	}
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::renderComponent(SpriteRenderComponent* componentView, type_index componentType)
{
	GARDEN_ASSERT(componentView);
	auto editorSystem = EditorRenderSystem::Instance::get();

	auto maxMipCount = componentView->useMipmap ? 0 : 1;
	auto flags = ImageLoadFlags::TypeArray | ImageLoadFlags::LoadShared;
	if (componentView->isArray) flags |= ImageLoadFlags::LoadArray;
	editorSystem->drawImageSelector("Color Map", componentView->colorMapPath, componentView->colorMap,
		componentView->descriptorSet, componentView->getEntity(), componentType, maxMipCount, flags);
	editorSystem->drawResource(componentView->descriptorSet);

	ImGui::Checkbox("Enabled", &componentView->isEnabled); ImGui::SameLine();

	auto reloadImage = false;
	if (ImGui::Checkbox("Array", &componentView->isArray))
		reloadImage = true;
	ImGui::SameLine();
	if (ImGui::Checkbox("Mipmap", &componentView->useMipmap))
		reloadImage = true;
	ImGui::SameLine();

	if (reloadImage && !componentView->colorMapPath.empty())
	{
		auto resourceSystem = ResourceSystem::Instance::get();
		resourceSystem->destroyShared(componentView->colorMap);
		resourceSystem->destroyShared(componentView->descriptorSet);

		auto maxMipCount = componentView->useMipmap ? 0 : 1;
		auto flags = ImageLoadFlags::TypeArray | ImageLoadFlags::LoadShared;
		if (componentView->isArray) flags |= ImageLoadFlags::LoadArray;
		auto usage = Image::Usage::Sampled | Image::Usage::TransferDst | Image::Usage::TransferQ;
		if (maxMipCount == 0) usage |= Image::Usage::TransferSrc;
		componentView->colorMap = resourceSystem->loadImage(componentView->colorMapPath, 
			usage, maxMipCount, Image::Strategy::Default, flags, componentView->taskPriority);
		componentView->descriptorSet = {};
	}

	ImGui::BeginDisabled();
	auto isVisible = componentView->isVisible();
	ImGui::Checkbox("Visible", &isVisible);
	ImGui::EndDisabled();
	ImGui::Spacing();

	auto maxColorMapLayer = 0.0f;
	if (componentView->colorMap)
	{
		auto colorMapView = GraphicsSystem::Instance::get()->get(componentView->colorMap);
		maxColorMapLayer = colorMapView->getLayerCount() - 1;
	}

	auto aabbMin = componentView->aabb.getMin(), aabbMax = componentView->aabb.getMax();
	if (ImGui::DragFloat3("Min AABB", &aabbMin, 0.01f))
		componentView->aabb.trySet(aabbMin, aabbMax);
	if (ImGui::BeginPopupContextItem("minAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->aabb = Aabb::one;
		ImGui::EndPopup();
	}

	if (ImGui::DragFloat3("Max AABB", &aabbMax, 0.01f))
		componentView->aabb.trySet(aabbMin, aabbMax);
	if (ImGui::BeginPopupContextItem("maxAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->aabb = Aabb::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Size", &componentView->uvSize, 0.01f);
	if (ImGui::BeginPopupContextItem("uvSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->uvSize = float2::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Offset", &componentView->uvOffset, 0.01f);
	if (ImGui::BeginPopupContextItem("uvOffset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->uvOffset = float2::zero;
		ImGui::EndPopup();
	}

	ImGui::SliderFloat("Color Map Layer", &componentView->colorMapLayer, 0.0f, maxColorMapLayer);
	if (ImGui::BeginPopupContextItem("colorLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->colorMapLayer = 0.0f;
		ImGui::EndPopup();
	}

	ImGui::ColorEdit4("Color", &componentView->color, ImGuiColorEditFlags_Float);
	if (ImGui::BeginPopupContextItem("colorFactor"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->color = f32x4::one;
		ImGui::EndPopup();
	}

	ImGui::Spacing();
	ImGui::DragFloat("Task Priority", &componentView->taskPriority, 0.1f);

	auto manager = Manager::Instance::get();
	ImGui::BeginDisabled(!componentView->colorMap && manager->has<TransformComponent>(componentView->getEntity()));
	if (ImGui::Button("Auto Scale", ImVec2(-FLT_MIN, 0.0f)))
	{
		auto transformView = TransformSystem::Instance::get()->getComponent(componentView->getEntity());
		auto colorMapView = GraphicsSystem::Instance::get()->get(componentView->colorMap);
		auto imageSize = colorMapView->getSize();

		if (imageSize.getX() > imageSize.getY())
		{
			transformView->scale(f32x4(componentView->uvSize.x * transformView->getScale().getY() * 
				((float)imageSize.getX() / imageSize.getY()), 1.0f, 1.0f));
		}
		else
		{
			transformView->scale(f32x4(1.0f, componentView->uvSize.y * transformView->getScale().getX() * 
				((float)imageSize.getY() / imageSize.getX()), 1.0f));
		}
	}
	ImGui::EndDisabled();
}
#endif
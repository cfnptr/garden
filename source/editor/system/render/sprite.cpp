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

#include "garden/editor/system/render/sprite.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/render/sprite/ui.hpp"
#include "garden/system/render/sprite/opaque.hpp"
#include "garden/system/render/sprite/cutout.hpp"
#include "garden/system/render/sprite/translucent.hpp"

using namespace garden;

//**********************************************************************************************************************
SpriteRenderEditorSystem::SpriteRenderEditorSystem()
{
	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("Init", SpriteRenderEditorSystem::init);
}
void SpriteRenderEditorSystem::init()
{
	auto editorSystem = EditorRenderSystem::getInstance();
	if (OpaqueSpriteSystem::hasInstance())
	{
		editorSystem->registerEntityInspector<OpaqueSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	if (CutoutSpriteSystem::hasInstance())
	{
		editorSystem->registerEntityInspector<CutoutSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (TransSpriteSystem::hasInstance())
	{
		editorSystem->registerEntityInspector<TransSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTransEntityInspector(entity, isOpened);
		});
	}
	if (UiSpriteSystem::hasInstance())
	{
		editorSystem->registerEntityInspector<UiSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onUiEntityInspector(entity, isOpened);
		});
	}
}

//**********************************************************************************************************************
template<class C>
static void renderSpriteTooltip(ID<Entity> entity)
{
	if (ImGui::BeginItemTooltip())
	{
		auto spriteView = Manager::getInstance()->get<C>(entity);
		ImGui::Text("Enabled: %s, Path: %s", spriteView->isEnabled ? "true" : "false",
			spriteView->colorMapPath.empty() ? "<null>" : spriteView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
}

void SpriteRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<OpaqueSpriteComponent>(entity);
	if (isOpened)
	{
		auto opaqueSpriteView = Manager::getInstance()->get<OpaqueSpriteComponent>(entity);
		renderComponent(*opaqueSpriteView, typeid(OpaqueSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<CutoutSpriteComponent>(entity);
	if (isOpened)
	{
		auto cutoutSpriteView = Manager::getInstance()->get<CutoutSpriteComponent>(entity);
		renderComponent(*cutoutSpriteView, typeid(CutoutSpriteComponent));

		auto alphaCutoff = cutoutSpriteView->getAlphaCutoff();
		if (ImGui::SliderFloat("Alpha Cutoff", &alphaCutoff, 0.0f, 1.0f))
			cutoutSpriteView->setAlphaCutoff(alphaCutoff);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cutoutSpriteView->setAlphaCutoff(0.5f);
			ImGui::EndPopup();
		}
	}
}
void SpriteRenderEditorSystem::onTransEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<TransSpriteComponent>(entity);
	if (isOpened)
	{
		auto transSpriteView = Manager::getInstance()->get<TransSpriteComponent>(entity);
		renderComponent(*transSpriteView, typeid(TransSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onUiEntityInspector(ID<Entity> entity, bool isOpened)
{
	renderSpriteTooltip<UiSpriteComponent>(entity);
	if (isOpened)
	{
		auto uiSpriteView = Manager::getInstance()->get<UiSpriteComponent>(entity);
		renderComponent(*uiSpriteView, typeid(UiSpriteComponent));
	}
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::renderComponent(SpriteRenderComponent* componentView, type_index componentType)
{
	GARDEN_ASSERT(componentView);
	auto editorSystem = EditorRenderSystem::getInstance();

	editorSystem->drawImageSelector("Color Map", componentView->colorMapPath, componentView->colorMap,
		componentView->descriptorSet, componentView->getEntity(), componentType);
	editorSystem->drawResource(componentView->descriptorSet);

	auto isEnabled = componentView->isEnabled;
	if (ImGui::Checkbox("Enabled", &isEnabled))
		componentView->isEnabled = isEnabled;
	ImGui::SameLine();

	if (!componentView->colorMapPath.empty())
	{
		auto resourceSystem = ResourceSystem::getInstance();
		resourceSystem->destroyShared(componentView->colorMap);
		resourceSystem->destroyShared(componentView->descriptorSet);
		componentView->colorMap = resourceSystem->loadSharedImage(componentView->colorMapPath);
	}

	ImGui::BeginDisabled();
	auto isVisible = componentView->isVisible;
	ImGui::Checkbox("Is Visible", &isVisible);
	ImGui::EndDisabled();
	ImGui::Spacing();

	auto maxColorMapLayer = 0.0f;
	if (componentView->colorMap)
	{
		auto colorMapView = GraphicsSystem::getInstance()->get(componentView->colorMap);
		maxColorMapLayer = colorMapView->getLayerCount() - 1;

		if (colorMapView->getType() != Image::Type::Texture2DArray)
		{
			ResourceSystem::getInstance()->destroyShared(componentView->colorMap);
			componentView->colorMapPath = "";
		}
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
			componentView->uvSize = half2::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Offset", &componentView->uvOffset, 0.01f);
	if (ImGui::BeginPopupContextItem("uvOffset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->uvOffset = half2::zero;
		ImGui::EndPopup();
	}

	auto colorMapLayer = componentView->getColorMapLayer();
	if (ImGui::SliderFloat("Color Map Layer", &colorMapLayer, 0.0f, maxColorMapLayer))
		componentView->setColorMapLayer(colorMapLayer);
	if (ImGui::BeginPopupContextItem("colorMapLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->setColorMapLayer(0.0f);
		ImGui::EndPopup();
	}

	ImGui::ColorEdit4("Color Add", &componentView->colorAdd, ImGuiColorEditFlags_Float);
	if (ImGui::BeginPopupContextItem("colorAdd"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->colorAdd = Color::transparent;
		ImGui::EndPopup();
	}

	ImGui::ColorEdit4("Color Mul", &componentView->colorMul, ImGuiColorEditFlags_Float);
	if (ImGui::BeginPopupContextItem("colorMul"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->colorMul = Color::white;
		ImGui::EndPopup();
	}

	auto manager = Manager::getInstance();
	ImGui::BeginDisabled(!componentView->colorMap && manager->has<TransformComponent>(componentView->getEntity()));
	if (ImGui::Button("Auto Scale", ImVec2(-FLT_MIN, 0.0f)))
	{
		auto transformView = manager->get<TransformComponent>(componentView->getEntity());
		auto colorMapView = GraphicsSystem::getInstance()->get(componentView->colorMap);
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
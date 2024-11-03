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
#include "garden/system/resource.hpp"
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
	if (TranslucentSpriteSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<TranslucentSpriteComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTranslucentEntityInspector(entity, isOpened);
		});
	}
}
void SpriteRenderEditorSystem::deinit()
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	editorSystem->tryUnregisterEntityInspector<OpaqueSpriteComponent>();
	editorSystem->tryUnregisterEntityInspector<CutoutSpriteComponent>();
	editorSystem->tryUnregisterEntityInspector<TranslucentSpriteComponent>();
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto opaqueSpriteView = OpaqueSpriteSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", opaqueSpriteView->isEnabled ? "true" : "false",
			opaqueSpriteView->colorMapPath.empty() ? "<null>" :
			opaqueSpriteView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto opaqueSpriteView = OpaqueSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*opaqueSpriteView, typeid(OpaqueSpriteComponent));
	}
}
void SpriteRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto cutoutSpriteView = CutoutSpriteSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", cutoutSpriteView->isEnabled ? "true" : "false",
			cutoutSpriteView->colorMapPath.empty() ? "<null>" :
			cutoutSpriteView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
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
void SpriteRenderEditorSystem::onTranslucentEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto translucentSpriteView = TranslucentSpriteSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", translucentSpriteView->isEnabled ? "true" : "false",
			translucentSpriteView->colorMapPath.empty() ? "<null>" :
			translucentSpriteView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto translucentSpriteView = TranslucentSpriteSystem::Instance::get()->getComponent(entity);
		renderComponent(*translucentSpriteView, typeid(TranslucentSpriteComponent));
	}
}

//**********************************************************************************************************************
void SpriteRenderEditorSystem::renderComponent(SpriteRenderComponent* componentView, type_index componentType)
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
	if (componentView->isArray)
		flags |= ImageLoadFlags::LoadArray;
	editorSystem->drawImageSelector(componentView->colorMapPath, componentView->colorMap,
		componentView->descriptorSet, componentView->getEntity(), componentType, flags);
	editorSystem->drawResource(componentView->descriptorSet);

	ImGui::Checkbox("Enabled", &componentView->isEnabled); ImGui::SameLine();

	if (ImGui::Checkbox("Array", &componentView->isArray) && !componentView->colorMapPath.empty())
	{
		auto resourceSystem = ResourceSystem::Instance::get();
		resourceSystem->destroyShared(componentView->colorMap);
		resourceSystem->destroyShared(componentView->descriptorSet);

		auto flags = ImageLoadFlags::ArrayType | ImageLoadFlags::LoadShared;
		if (componentView->isArray)
			flags |= ImageLoadFlags::LoadArray;
		componentView->colorMap = resourceSystem->loadImage(componentView->colorMapPath,
			Image::Bind::TransferDst | Image::Bind::Sampled, 1, Image::Strategy::Default, flags);
		componentView->descriptorSet = {};
	}

	auto transformSystem = TransformSystem::Instance::get();
	ImGui::BeginDisabled(!componentView->colorMap && transformSystem->hasComponent(componentView->getEntity()));
	if (ImGui::Button("Auto Scale", ImVec2(-FLT_MIN, 0.0f)))
	{
		auto transformView = transformSystem->getComponent(componentView->getEntity());
		auto colorMapView = GraphicsSystem::Instance::get()->get(componentView->colorMap);
		auto imageSize = colorMapView->getSize();

		if (imageSize.x > imageSize.y)
		{
			transformView->scale.x = componentView->uvSize.x *
				transformView->scale.y * ((float)imageSize.x / imageSize.y);
		}
		else
		{
			transformView->scale.y = componentView->uvSize.y *
				transformView->scale.x * ((float)imageSize.y / imageSize.x);
		}
	}
	ImGui::EndDisabled();

	auto maxColorMapLayer = 0.0f;
	if (componentView->colorMap)
	{
		auto colorMapView = GraphicsSystem::Instance::get()->get(componentView->colorMap);
		maxColorMapLayer = colorMapView->getLayerCount() - 1;
	}

	auto& aabb = componentView->aabb;
	ImGui::DragFloat3("Min AABB", (float3*)&aabb.getMin(), 0.01f);
	if (ImGui::BeginPopupContextItem("minAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			aabb = Aabb::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat3("Max AABB", (float3*)&aabb.getMax(), 0.01f);
	if (ImGui::BeginPopupContextItem("maxAabb"))
	{
		if (ImGui::MenuItem("Reset Default"))
			aabb = Aabb::one;
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Size", &componentView->uvSize, 0.01f);
	if (ImGui::BeginPopupContextItem("uvSize"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->uvSize = float2(1.0f);
		ImGui::EndPopup();
	}

	ImGui::DragFloat2("UV Offset", &componentView->uvOffset, 0.01f);
	if (ImGui::BeginPopupContextItem("uvOffset"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->uvOffset = float2(0.0f);
		ImGui::EndPopup();
	}

	ImGui::SliderFloat("Color Map Layer", &componentView->colorMapLayer, 0.0f, maxColorMapLayer);
	if (ImGui::BeginPopupContextItem("colorLayer"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->colorMapLayer = 0.0f;
		ImGui::EndPopup();
	}

	ImGui::SliderFloat4("Color Factor", &componentView->colorFactor, 0.0f, 1.0f);
	if (ImGui::BeginPopupContextItem("colorFactor"))
	{
		if (ImGui::MenuItem("Reset Default"))
			componentView->colorFactor = float4(1.0f);
		ImGui::EndPopup();
	}
}
#endif
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

#include "garden/editor/system/render/model.hpp"
#include "imgui.h"
#include "math/simd/vector/float.hpp"

#if GARDEN_EDITOR
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/render/model/color.hpp"
#include "garden/system/render/model/opaque.hpp"
#include "garden/system/render/model/cutout.hpp"
#include "garden/system/render/model/translucent.hpp"

using namespace garden;

ModelRenderEditorSystem::ModelRenderEditorSystem()
{
	ECSM_SUBSCRIBE_TO_EVENT("Init", ModelRenderEditorSystem::init);
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", ModelRenderEditorSystem::deinit);
}
ModelRenderEditorSystem::~ModelRenderEditorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		ECSM_UNSUBSCRIBE_FROM_EVENT("Init", ModelRenderEditorSystem::init);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", ModelRenderEditorSystem::deinit);
	}
}

void ModelRenderEditorSystem::init()
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	if (ColorModelSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<ColorModelComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onColorEntityInspector(entity, isOpened);
		});
	}
	if (OpaqueModelSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<OpaqueModelComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onOpaqueEntityInspector(entity, isOpened);
		});
	}
	if (CutoutModelSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<CutoutModelComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onCutoutEntityInspector(entity, isOpened);
		});
	}
	if (TransModelSystem::Instance::has())
	{
		editorSystem->registerEntityInspector<TransModelComponent>(
		[this](ID<Entity> entity, bool isOpened)
		{
			onTransEntityInspector(entity, isOpened);
		});
	}
}
void ModelRenderEditorSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto editorSystem = EditorRenderSystem::Instance::get();
		editorSystem->tryUnregisterEntityInspector<ColorModelComponent>();
		editorSystem->tryUnregisterEntityInspector<OpaqueModelComponent>();
		editorSystem->tryUnregisterEntityInspector<CutoutModelComponent>();
		editorSystem->tryUnregisterEntityInspector<TransModelComponent>();
	}
}

//**********************************************************************************************************************
void ModelRenderEditorSystem::onColorEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto colorModelView = ColorModelSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", colorModelView->isEnabled ? "true" : "false",
			colorModelView->colorMapPath.empty() ? "<null>" :
			colorModelView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto colorModelView = ColorModelSystem::Instance::get()->getComponent(entity);
		renderComponent(*colorModelView, typeid(ColorModelComponent));

		ImGui::ColorEdit4("Color", &colorModelView->color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (ImGui::BeginPopupContextItem("color"))
		{
			if (ImGui::MenuItem("Reset Default"))
				colorModelView->color = f32x4::one;
			ImGui::EndPopup();
		}
	}
}
void ModelRenderEditorSystem::onOpaqueEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto opaqueModelView = OpaqueModelSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", opaqueModelView->isEnabled ? "true" : "false",
			opaqueModelView->colorMapPath.empty() ? "<null>" :
			opaqueModelView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto opaqueModelView = OpaqueModelSystem::Instance::get()->getComponent(entity);
		renderComponent(*opaqueModelView, typeid(OpaqueModelComponent));
	}
}
void ModelRenderEditorSystem::onCutoutEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto cutoutModelView = CutoutModelSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", cutoutModelView->isEnabled ? "true" : "false",
			cutoutModelView->colorMapPath.empty() ? "<null>" :
			cutoutModelView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto cutoutModelView = CutoutModelSystem::Instance::get()->getComponent(entity);
		renderComponent(*cutoutModelView, typeid(CutoutModelComponent));

		ImGui::SliderFloat("Alpha Cutoff", &cutoutModelView->alphaCutoff, 0.0f, 1.0f);
		if (ImGui::BeginPopupContextItem("alphaCutoff"))
		{
			if (ImGui::MenuItem("Reset Default"))
				cutoutModelView->alphaCutoff = 0.5f;
			ImGui::EndPopup();
		}
	}
}
void ModelRenderEditorSystem::onTransEntityInspector(ID<Entity> entity, bool isOpened)
{
	if (ImGui::BeginItemTooltip())
	{
		auto transModelView = TransModelSystem::Instance::get()->getComponent(entity);
		ImGui::Text("Enabled: %s, Path: %s", transModelView->isEnabled ? "true" : "false",
			transModelView->colorMapPath.empty() ? "<null>" :
			transModelView->colorMapPath.generic_string().c_str());
		ImGui::EndTooltip();
	}
	if (isOpened)
	{
		auto transModelView = TransModelSystem::Instance::get()->getComponent(entity);
		renderComponent(*transModelView, typeid(TransModelComponent));
	}
}

//**********************************************************************************************************************
void ModelRenderEditorSystem::renderComponent(ModelRenderComponent* componentView, type_index componentType)
{
	auto editorSystem = EditorRenderSystem::Instance::get();
	auto flags = ImageLoadFlags::LoadShared;
	editorSystem->drawImageSelector(componentView->colorMapPath, componentView->colorMap,
		componentView->descriptorSet, componentView->getEntity(), componentType, flags);
	editorSystem->drawResource(componentView->descriptorSet);

	ImGui::Checkbox("Enabled", &componentView->isEnabled);

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
}
#endif
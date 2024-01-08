//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/render/editor/geometry.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"

using namespace garden;

//--------------------------------------------------------------------------------------------------
GeometryEditor::GeometryEditor(GeometryRenderSystem* system)
{
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void GeometryEditor::renderInfo(GeometryRenderComponent* geometryComponent,
	float* alphaCutoff)
{
	// TODO: push(name) and pop id. pass type_info in the argument.

	ImGui::Checkbox("Enabled", &geometryComponent->isEnabled);
	
	auto& aabb = geometryComponent->aabb;
	ImGui::DragFloat3("Min AABB", (float*)&aabb.getMin(), 0.01f);
	ImGui::DragFloat3("Max AABB", (float*)&aabb.getMax(), 0.01f);
	ImGui::SliderFloat4("Base Color Factor",
		(float*)&geometryComponent->baseColorFactor, 0.0f, 1.0f);
	ImGui::SliderFloat3("Emissive Factor",
		(float*)&geometryComponent->emissiveFactor, 0.0f, 1.0f);
	ImGui::SliderFloat("Metallic Factor",
		(float*)&geometryComponent->metallicFactor, 0.0f, 1.0f);
	ImGui::SliderFloat("Roughness Factor",
		(float*)&geometryComponent->roughnessFactor, 0.0f, 1.0f);
	ImGui::SliderFloat("Reflectance Factor",
		(float*)&geometryComponent->reflectanceFactor, 0.0f, 1.0f);
	if (alphaCutoff) ImGui::SliderFloat("Alpha Cutoff", alphaCutoff, 0.0f, 1.0f);
	ImGui::Spacing(); ImGui::Separator();

	if (aabb.getMin().x > aabb.getMax().x || aabb.getMin().y > aabb.getMax().y ||
		aabb.getMin().z > aabb.getMax().z)
	{
		aabb.setMin(min(aabb.getMin(), aabb.getMax()));
	}

	auto graphicsSystem = system->getGraphicsSystem();
	if (geometryComponent->vertexBuffer)
	{
		auto bufferView = graphicsSystem->get(geometryComponent->vertexBuffer);
		auto stringOffset = bufferView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Vertex Buffer: %d (%s)", *geometryComponent->vertexBuffer,
			bufferView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Vertex Buffer: null");

	if (geometryComponent->indexBuffer)
	{
		auto bufferView = graphicsSystem->get(geometryComponent->indexBuffer);
		auto stringOffset = bufferView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Index Buffer: %d (%s)", *geometryComponent->indexBuffer,
			bufferView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Index Buffer: null");

	if (geometryComponent->baseColorMap)
	{
		auto imageView = graphicsSystem->get(geometryComponent->baseColorMap);
		auto stringOffset = imageView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Base Color Map: %d (%s)", *geometryComponent->baseColorMap,
			imageView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Base Color Map: null");

	if (geometryComponent->ormMap)
	{
		auto imageView = graphicsSystem->get(geometryComponent->ormMap);
		auto stringOffset = imageView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Occlusion/Roughness/Metallic Map: %d (%s)",
			*geometryComponent->ormMap,
			imageView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Occlusion/Roughness/Metallic Map: null");

	if (geometryComponent->descriptorSet)
	{
		auto descriptorSetView = graphicsSystem->get(geometryComponent->descriptorSet);
		auto stringOffset = descriptorSetView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Descriptor Set: %d (%s)", *geometryComponent->descriptorSet,
			descriptorSetView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Descriptor Set: null");
		
	ImGui::Text("Index Count: %d, Offset: %d",
		geometryComponent->indexCount, geometryComponent->indexOffset);
	ImGui::Text("Triangle Count: %d", geometryComponent->indexCount / 3);

	auto manager = system->getManager();
	auto editorSystem = manager->get<EditorRenderSystem>();
	editorSystem->selectedEntityAabb = geometryComponent->aabb;
}

//--------------------------------------------------------------------------------------------------
GeometryShadowEditor::GeometryShadowEditor(GeometryShadowRenderSystem* system)
{
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void GeometryShadowEditor::renderInfo(
	GeometryShadowRenderComponent* geometryShadowComponent)
{
	auto graphicsSystem = system->getGraphicsSystem();
	auto& aabb = geometryShadowComponent->aabb;

	ImGui::Checkbox("Enabled", &geometryShadowComponent->isEnabled);
	ImGui::DragFloat3("Min AABB", (float*)&aabb.getMin(), 0.01f);
	ImGui::DragFloat3("Max AABB", (float*)&aabb.getMax(), 0.01f);
	ImGui::Spacing(); ImGui::Separator();

	if (aabb.getMin().x > aabb.getMax().x || aabb.getMin().y > aabb.getMax().y ||
		aabb.getMin().z > aabb.getMax().z)
	{
		aabb.setMin(min(aabb.getMin(), aabb.getMax()));
	}

	if (geometryShadowComponent->vertexBuffer)
	{
		auto bufferView = graphicsSystem->get(geometryShadowComponent->vertexBuffer);
		auto stringOffset = bufferView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Vertex Buffer: %d (%s)", *geometryShadowComponent->vertexBuffer,
			bufferView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Vertex Buffer: null");

	if (geometryShadowComponent->indexBuffer)
	{
		auto bufferView = graphicsSystem->get(geometryShadowComponent->indexBuffer);
		auto stringOffset = bufferView->getDebugName().find_last_of('.');
		if (stringOffset == string::npos) stringOffset = 0; else stringOffset++;
		ImGui::Text("Index Buffer: %d (%s)", *geometryShadowComponent->indexBuffer,
			bufferView->getDebugName().c_str() + stringOffset);
	}
	else ImGui::Text("Index Buffer: null");
		
	ImGui::Text("Index Count: %d, Offset: %d",
		geometryShadowComponent->indexCount, geometryShadowComponent->indexOffset);
	ImGui::Text("Triangle Count: %d", geometryShadowComponent->indexCount / 3);
}
#endif
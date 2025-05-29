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

#pragma once
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
namespace garden
{

class MeshGizmosEditorSystem final : public System
{
public:
	struct PushConstants final
	{
		float4x4 mvp;
		float3 color;
		float patternScale;
	};
	struct GizmosMesh final
	{
		f32x4x4 model = f32x4x4::zero;
		Color color = Color::black;
		ID<Buffer> vertexBuffer = {};
		uint32 vertexCount = 0;
		float distance = 0.0f;
	};
private:
	vector<GizmosMesh> gizmosMeshes;
	ID<GraphicsPipeline> frontGizmosPipeline = {};
	ID<GraphicsPipeline> backGizmosPipeline = {};
	ID<Buffer> arrowVertexBuffer = {};
	uint32 dragMode = 0;

	MeshGizmosEditorSystem();
	~MeshGizmosEditorSystem() final;

	void init();
	void deinit();
	void render();
	void editorSettings();
	
	friend class ecsm::Manager;
public:
	Color handleColor = Color("F0F0F0FF");
	Color axisColorX = Color("FF1010FF");
	Color axisColorY = Color("10FF10FF");
	Color axisColorZ = Color("1010FFFF");
	float highlightFactor = 5.0f;
	float patternScale = 0.25f;
	bool isEnabled = true;
};

} // namespace garden
#endif



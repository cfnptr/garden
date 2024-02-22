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

#pragma once
#include "garden/system/render/mesh.hpp"

#if GARDEN_EDITOR
namespace garden
{

using namespace garden;
using namespace garden::graphics;
class SelectorEditorSystem;

class GizmosEditor final
{
	MeshRenderSystem* system = nullptr;
	ID<GraphicsPipeline> frontGizmosPipeline = {};
	ID<GraphicsPipeline> backGizmosPipeline = {};
	ID<Buffer> fullArrowVertices = {};
	float2 lastCursorPos = float2(0.0f);
	uint32 dragMode = 0;

	GizmosEditor(MeshRenderSystem* system);
	void preSwapchainRender();
	
	friend class SelectorEditorSystem;
	friend class MeshRenderSystem;
};

} // namespace garden
#endif



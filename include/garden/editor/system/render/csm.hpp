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

class CsmRenderEditorSystem final : public System
{
	struct PushConstants final
	{
		float3 farPlanes;
	};

	ID<GraphicsPipeline> cascadesPipeline = {};
	ID<DescriptorSet> cascadesDS = {};
	bool visualizeCascades = false;
	bool showWindow = false;

	CsmRenderEditorSystem();
	~CsmRenderEditorSystem();

	void init();
	void deinit();
	void preUiRender();
	void uiRender();
	void gBufferRecreate();
	void editorBarToolPP();
	
	friend class ecsm::Manager;
};

} // namespace garden
#endif
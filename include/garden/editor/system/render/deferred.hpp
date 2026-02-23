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

#pragma once
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "editor/gbuffer-data.h"
#include "common/gbuffer.h"

namespace garden
{

class DeferredRenderEditorSystem final : public System
{
	struct BufferPC final
	{
		float4x4 invViewProj;
		float showChannelR;
		float showChannelG;
		float showChannelB;
	};
	struct LightingPC final
	{
		float3 baseColor = float3::one;
		float specularFactor = 1.0f;
		float4 mraor = float4(0.0f, 1.0f, 1.0f, 0.5f);
		float shadowAlpha = 1.0f;
		float emissiveFactor = 0.0f;
		float clearCoat = 0.0f;
		float clearCoatRoughness = 0.0f;
		uint32 materialID = G_MATERIAL_BASE;
	};

	ID<Image> blackPlaceholder = {};
	ID<GraphicsPipeline> bufferPipeline = {};
	ID<GraphicsPipeline> pbrLightingPipeline = {};
	ID<DescriptorSet> bufferDescriptorSet = {};
	LightingPC lightingPC = {};
	float ambientOcclusion = 1.0f;
	uint8 drawMode = G_BUFFER_DRAW_MODE_OFF;
	bool showChannelR = true;
	bool showChannelG = true;
	bool showChannelB = true;
	bool showWindow = false;

	DeferredRenderEditorSystem();
	~DeferredRenderEditorSystem() final;

	void init();
	void deinit();
	void deferredRender();
	void preLdrRender();
	void ldrRender();
	void gBufferRecreate();
	void editorBarTool();
	
	friend class ecsm::Manager;
};

} // namespace garden
#endif
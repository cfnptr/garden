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

class DeferredRenderEditorSystem final : public System
{
	enum class DrawMode : uint8
	{
		Off, BaseColor, SpecularFactor, Transmission, Metallic, Roughness, MaterialAO, Reflectance, 
		ClearCoatRoughness, Normals, MaterialShadows, EmissiveColor, EmissiveFactor, GiColor, 
		Lighting, HdrBuffer, OitAccumColor, OitAccumAlpha, OitRevealage, Depth, WorldPositions, 
		GlobalShadowColor, GlobalShadowAlpha, GlobalAO, DenoisedGlobalAO, Count
	};

	struct BufferPC final
	{
		float4x4 invViewProj;
		int32 drawMode;
		float showChannelR;
		float showChannelG;
		float showChannelB;
	};
	struct LightingPC final
	{
		float3 baseColor = float3::one;
		float specularFactor = 1.0f;
		float4 mraor = float4(0.0f, 1.0f, 1.0f, 0.5f);
		float3 emissiveColor = float3::zero;
		float emissiveFactor = 0.0f;
		float3 giColor = float3::one;
		float ccRoughness = 0.0f;
		float shadow = 1.0f;
	};

	ID<Image> blackPlaceholder = {};
	ID<GraphicsPipeline> bufferPipeline = {};
	ID<GraphicsPipeline> pbrLightingPipeline = {};
	ID<DescriptorSet> bufferDescriptorSet = {};
	LightingPC lightingPC = {};
	DrawMode drawMode = DrawMode::Off;
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
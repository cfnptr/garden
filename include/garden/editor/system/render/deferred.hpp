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
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
namespace garden
{

class DeferredRenderEditorSystem final : public System
{
	enum class DrawMode : uint8
	{
		Off, HDR, BaseColor, Metallic, Roughness, Reflectance,
		Emissive, Normal, WorldPosition, Depth, Lighting,
		Shadow, AmbientOcclusion, AmbientOcclusionD, Count
	};

	struct BufferPC final
	{
		float4x4 viewProjInv;
		int32 drawMode;
		float showChannelR;
		float showChannelG;
		float showChannelB;
	};
	struct LightingPC final
	{
		float4 baseColor;
		float4 emissive;
		float metallic;
		float roughness;
		float reflectance;
	};

	ID<Image> shadowPlaceholder = {};
	ID<GraphicsPipeline> bufferPipeline = {};
	ID<GraphicsPipeline> pbrLightingPipeline = {};
	ID<DescriptorSet> bufferDescriptorSet = {};
	float4 baseColorOverride = float4(1.0f);
	float4 emissiveOverride = float4(0.0f);
	float metallicOverride = 0.0f;
	float roughnessOverride = 1.0f;
	float reflectanceOverride = 0.5f;
	DrawMode drawMode = DrawMode::Off;
	bool showChannelR = true;
	bool showChannelG = true;
	bool showChannelB = true;
	bool showWindow = false;

	DeferredRenderEditorSystem();
	~DeferredRenderEditorSystem() final;

	void init();
	void deinit();
	void editorRender();
	void deferredRender();
	void preLdrRender();
	void ldrRender();
	void gBufferRecreate();
	void editorBarTool();
	
	friend class ecsm::Manager;
};

} // namespace garden
#endif
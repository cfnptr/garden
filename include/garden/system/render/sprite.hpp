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
#include "garden/system/render/instance.hpp"

namespace garden
{

using namespace garden::graphics;
class SpriteRenderSystem;

/***********************************************************************************************************************
 * @brief Sprite mesh rendering data container.
 */
struct SpriteRenderComponent : public MeshRenderComponent
{
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};
	float4 colorFactor = float4(1.0f);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	string path;
	#endif
};

/***********************************************************************************************************************
 * @brief Sprite mesh rendering system.
 */
class SpriteRenderSystem : public InstanceRenderSystem
{
public:
	struct InstanceData
	{
		float4x4 mvp = float4x4(0.0f);
	};
protected:
	/**
	 * @brief Creates a new sprite rendering system instance.
	 * @param[in,out] manager manager instance
	 */
	SpriteRenderSystem(Manager* manager) : InstanceRenderSystem(manager) { }
	
	bool isDrawReady() override;
	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) override;

	void setDescriptorSetRange(MeshRenderComponent* meshRenderComponent,
		DescriptorSet::Range* range, uint8& index, uint8 capacity) override;
	map<string, DescriptorSet::Uniform> getDefaultUniforms() override;
	void destroyResources(SpriteRenderComponent* spriteComponent);
public:
	uint64 getInstanceDataSize() override;
};

} // namespace garden
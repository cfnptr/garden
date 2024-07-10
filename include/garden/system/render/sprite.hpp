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

/***********************************************************************************************************************
 * @brief Sprite mesh rendering data container.
 */
struct SpriteRenderComponent : public MeshRenderComponent
{
protected:
	uint16 _alignment0 = 0;
public:
	bool isArray = false;
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};
	float colorMapLayer = 0.0f;
	float4 colorFactor = float4(1.0f);
	float2 uvSize = float2(1.0f);
	float2 uvOffset = float2(0.0f);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	string path;
	#endif
};

/***********************************************************************************************************************
 * @brief Sprite mesh animation frame container.
 */
struct SpriteRenderFrame : public AnimationFrame
{
protected:
	uint8 _alignment0 = 0;
public:
	bool isEnabled = true;
	bool animateIsEnabled = false;
	bool animateColorFactor = false;
	bool animateUvSize = false;
	bool animateUvOffset = false;
	bool animateColorMapLayer = false;
	float4 colorFactor = float4(1.0f);
	float2 uvSize = float2(1.0f);
	float2 uvOffset = float2(0.0f);
	float colorMapLayer = 0.0f;
};

/***********************************************************************************************************************
 * @brief Sprite mesh rendering system.
 */
class SpriteRenderSystem : public InstanceRenderSystem, public ISerializable, public IAnimatable
{
public:
	struct InstanceData
	{
		float4x4 mvp = float4x4(0.0f);
		float4 colorFactor = float4(0.0f);
		float4 sizeOffset = float4(0.0f);
	};
	struct PushConstants
	{
		uint32 instanceIndex;
		float colorMapLayer;
	};
protected:
	ID<ImageView> defaultImageView = {};

	void init() override;
	void deinit() override;
	virtual void imageLoaded();

	void copyComponent(View<Component> source, View<Component> destination) override;

	bool isDrawReady() override;
	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) override;

	uint64 getInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderComponent, InstanceData* instanceData,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex);
	void setDescriptorSetRange(MeshRenderComponent* meshRenderComponent,
		DescriptorSet::Range* range, uint8& index, uint8 capacity) override;
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderComponent, PushConstants* pushConstants, 
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual map<string, DescriptorSet::Uniform> getSpriteUniforms(ID<ImageView> colorMap);
	map<string, DescriptorSet::Uniform> getDefaultUniforms() override;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, SpriteRenderFrame& frame);
public:
	static void tryDestroyResources(View<SpriteRenderComponent> spriteComponent);
};

} // namespace garden
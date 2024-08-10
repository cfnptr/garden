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
#include "garden/hash.hpp"
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
	fs::path colorMapPath = {};
	#endif
};

/***********************************************************************************************************************
 * @brief Sprite mesh animation frame container.
 */
struct SpriteRenderFrame : public AnimationFrame
{
	uint8 isEnabled : 1;
	uint8 animateIsEnabled : 1;
	uint8 animateColorFactor : 1;
	uint8 animateUvSize : 1;
	uint8 animateUvOffset : 1;
	uint8 animateColorMapLayer : 1;
	uint8 animateColorMap : 1;
	uint8 isArray : 1;
protected:
	uint16 _alignment0 = 0;
public:
	float4 colorFactor = float4(1.0f);
	float2 uvSize = float2(1.0f);
	float2 uvOffset = float2(0.0f);
	float colorMapLayer = 0.0f;
	Ref<Image> colorMap = {};
	Ref<DescriptorSet> descriptorSet = {};

	#if GARDEN_DEBUG || GARDEN_EDITOR
	fs::path colorMapPath = {};
	#endif

	SpriteRenderFrame() : isEnabled(true), animateIsEnabled(false), animateColorFactor(false), animateUvSize(false), 
		animateUvOffset(false), animateColorMapLayer(false), animateColorMap(false), isArray(false) { }
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
	fs::path pipelinePath = {};
	Hash128::State hashState = nullptr;
	ID<ImageView> defaultImageView = {};
	bool deferredBuffer = false;
	bool linearFilter = false;

	SpriteRenderSystem(const fs::path& pipelinePath, bool useDeferredBuffer, bool useLinearFilter);
	~SpriteRenderSystem() override;

	void init() override;
	void deinit() override;

	Ref<DescriptorSet> createSharedDS(ID<Image> image, const string& path);
	virtual void imageLoaded();

	void copyComponent(View<Component> source, View<Component> destination) override;

	bool isDrawReady() override;
	void drawAsync(MeshRenderComponent* meshRenderView, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) override;

	uint64 getInstanceDataSize() override;
	virtual void setInstanceData(SpriteRenderComponent* spriteRenderView, InstanceData* instanceData,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex);
	void setDescriptorSetRange(MeshRenderComponent* meshRenderView,
		DescriptorSet::Range* range, uint8& index, uint8 capacity) override;
	virtual void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex);
	virtual map<string, DescriptorSet::Uniform> getSpriteUniforms(ID<ImageView> colorMap);
	map<string, DescriptorSet::Uniform> getDefaultUniforms() override;
	ID<GraphicsPipeline> createPipeline() final;

	virtual LinearPool<SpriteRenderFrame>& getFrameComponentPool() = 0;
	virtual psize getFrameComponentSize() const = 0;

	void serialize(ISerializer& serializer, const View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) override;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) override;
	static void deserializeAnimation(IDeserializer& deserializer, SpriteRenderFrame& frame);
	static void destroyResources(View<SpriteRenderFrame> frameView);

	static void destroyResources(View<SpriteRenderComponent> spriteRenderView);
};

} // namespace garden
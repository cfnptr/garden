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
#include "garden/system/render/sprite.hpp"

namespace garden
{

using namespace garden::graphics;

struct CutoutSpriteComponent final : public SpriteRenderComponent
{
	float alphaCutoff = 0.5f;
};

struct CutoutSpriteFrame final : public SpriteRenderFrame
{
	float alphaCutoff = 0.5f;
	bool animateAlphaCutoff = false;
private:
	uint8 _alignment1 = 0;
	uint16 _alignment2 = 0;
};

class CutoutSpriteSystem final : public SpriteRenderSystem
{
public:
	struct CutoutInstanceData : public InstanceData
	{
		float4 colorFactor = float4(0.0f);
		float4 sizeOffset = float4(0.0f);
	};
private:
	LinearPool<CutoutSpriteComponent, false> components;
	LinearPool<CutoutSpriteFrame, false> animationFrames;
	bool deferredBuffer = false;
	bool linearFilter = false;

	/**
	 * @brief Creates a new cutout sprite rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 */
	CutoutSpriteSystem(bool useDeferredBuffer = false, bool useLinearFilter = true);

	void draw(MeshRenderComponent* meshRenderComponent, const float4x4& viewProj,
		const float4x4& model, uint32 drawIndex, int32 taskIndex) final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(ID<Component> source, ID<Component> destination) final;
	ID<GraphicsPipeline> createPipeline() final;

	void serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component) override;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) override;

	void serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	void animateAsync(ID<Entity> entity, ID<AnimationFrame> a, ID<AnimationFrame> b, float t) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;
	
	friend class ecsm::Manager;
public:
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;
	MeshRenderType getMeshRenderType() const final;
	const LinearPool<MeshRenderComponent>& getMeshComponentPool() const final;
	psize getMeshComponentSize() const final;
	uint64 getInstanceDataSize() final;
};

} // namespace garden
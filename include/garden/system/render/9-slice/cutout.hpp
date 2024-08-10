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
#include "garden/system/render/9-slice.hpp"

namespace garden
{

using namespace garden::graphics;

struct Cutout9SliceComponent final : public NineSliceRenderComponent
{
	float alphaCutoff = 0.5f;
};
struct Cutout9SliceFrame final : public NineSliceRenderFrame
{
	float alphaCutoff = 0.5f;
	bool animateAlphaCutoff = false;
};

class Cutout9SliceSystem final : public NineSliceRenderSystem
{
public:
	struct CutoutPushConstants final : public PushConstants
	{
		float alphaCutoff;
	};
private:
	LinearPool<Cutout9SliceComponent, false> components;
	LinearPool<Cutout9SliceFrame, false> animationFrames;
	bool deferredBuffer = false;
	bool linearFilter = false;

	/**
	 * @brief Creates a new cutout 9-slice rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 */
	Cutout9SliceSystem(bool useDeferredBuffer = false, bool useLinearFilter = true);

	void setPushConstants(SpriteRenderComponent* spriteRenderView, PushConstants* pushConstants,
		const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex) final;

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	void copyComponent(View<Component> source, View<Component> destination) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	MeshRenderType getMeshRenderType() const final;
	LinearPool<MeshRenderComponent>& getMeshComponentPool() final;
	psize getMeshComponentSize() const final;
	LinearPool<SpriteRenderFrame>& getFrameComponentPool() final;
	psize getFrameComponentSize() const final;

	void serialize(ISerializer& serializer, const View<Component> component) final;
	void deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component) final;

	void serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame) final;
	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) final;
	void animateAsync(View<Component> component,
		View<AnimationFrame> a, View<AnimationFrame> b, float t) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
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

struct OpaqueSpriteComponent final : public SpriteRenderComponent { };
struct OpaqueSpriteFrame final : public SpriteRenderFrame { };

class OpaqueSpriteSystem final : public SpriteRenderSystem
{
	LinearPool<OpaqueSpriteComponent, false> components;
	LinearPool<OpaqueSpriteFrame, false> animationFrames;
	bool deferredBuffer = false;
	bool linearFilter = false;

	/**
	 * @brief Creates a new opaque sprite rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 */
	OpaqueSpriteSystem(bool useDeferredBuffer = false, bool useLinearFilter = true);

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	MeshRenderType getMeshRenderType() const final;
	const LinearPool<MeshRenderComponent>& getMeshComponentPool() const final;
	psize getMeshComponentSize() const final;
	ID<GraphicsPipeline> createPipeline() final;

	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
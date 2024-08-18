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

struct Translucent9SliceComponent final : public NineSliceRenderComponent { };
struct Translucent9SliceFrame final : public NineSliceRenderFrame { };

class Translucent9SliceSystem final : public NineSliceRenderSystem
{
	uint16 _alignment = 0;
	LinearPool<Translucent9SliceComponent, false> components;
	LinearPool<Translucent9SliceFrame, false> animationFrames;

	/**
	 * @brief Creates a new translucent 9-slice rendering system instance.
	 * 
	 * @param useDeferredBuffer use deferred or forward framebuffer
	 * @param useLinearFilter use linear filtering for texture
	 */
	Translucent9SliceSystem(bool useDeferredBuffer = false, bool useLinearFilter = true);

	ID<Component> createComponent(ID<Entity> entity) final;
	void destroyComponent(ID<Component> instance) final;
	const string& getComponentName() const final;
	type_index getComponentType() const final;
	View<Component> getComponent(ID<Component> instance) final;
	void disposeComponents() final;

	MeshRenderType getMeshRenderType() const final;
	LinearPool<MeshRenderComponent>& getMeshComponentPool() final;
	psize getMeshComponentSize() const final;
	LinearPool<SpriteRenderFrame>& getFrameComponentPool() final;
	psize getFrameComponentSize() const final;

	ID<AnimationFrame> deserializeAnimation(IDeserializer& deserializer) final;
	View<AnimationFrame> getAnimation(ID<AnimationFrame> frame) final;
	void destroyAnimation(ID<AnimationFrame> frame) final;
	
	friend class ecsm::Manager;
};

} // namespace garden
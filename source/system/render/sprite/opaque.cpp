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

#include "garden/system/render/sprite/opaque.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
OpaqueSpriteSystem::OpaqueSpriteSystem(bool useDeferredBuffer, bool useLinearFilter) :
	SpriteRenderSystem("sprite/opaque", useDeferredBuffer, useLinearFilter) { }

ID<Component> OpaqueSpriteSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void OpaqueSpriteSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<OpaqueSpriteComponent>(instance));
	destroyResources(View<SpriteRenderComponent>(componentView));
	components.destroy(ID<OpaqueSpriteComponent>(instance));
}
const string& OpaqueSpriteSystem::getComponentName() const
{
	static const string name = "Opaque Sprite";
	return name;
}
type_index OpaqueSpriteSystem::getComponentType() const
{
	return typeid(OpaqueSpriteComponent);
}
View<Component> OpaqueSpriteSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<OpaqueSpriteComponent>(instance)));
}
void OpaqueSpriteSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

MeshRenderType OpaqueSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
LinearPool<MeshRenderComponent>& OpaqueSpriteSystem::getMeshComponentPool()
{
	return *((LinearPool<MeshRenderComponent>*)&components);
}
psize OpaqueSpriteSystem::getMeshComponentSize() const
{
	return sizeof(OpaqueSpriteComponent);
}
LinearPool<SpriteRenderFrame>& OpaqueSpriteSystem::getFrameComponentPool()
{
	return *((LinearPool<SpriteRenderFrame>*)&animationFrames);
}
psize OpaqueSpriteSystem::getFrameComponentSize() const
{
	return sizeof(OpaqueSpriteFrame);
}

//**********************************************************************************************************************
ID<AnimationFrame> OpaqueSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	OpaqueSpriteFrame frame;
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);

	if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize || 
		frame.animateUvOffset || frame.animateColorMapLayer || frame.animateColorMap)
	{
		return ID<AnimationFrame>(animationFrames.create(frame));
	}

	return {};
}
View<AnimationFrame> OpaqueSpriteSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<OpaqueSpriteFrame>(frame)));
}
void OpaqueSpriteSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	auto frameView = animationFrames.get(ID<OpaqueSpriteFrame>(frame));
	destroyResources(View<SpriteRenderFrame>(frameView));
	animationFrames.destroy(ID<OpaqueSpriteFrame>(frame));
}
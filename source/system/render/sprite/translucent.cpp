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

#include "garden/system/render/sprite/translucent.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
TranslucentSpriteSystem::TranslucentSpriteSystem(bool useDeferredBuffer, bool useLinearFilter, bool setSingleton) :
	SpriteRenderSystem("sprite/translucent", useDeferredBuffer, useLinearFilter), Singleton(setSingleton) { }
TranslucentSpriteSystem::~TranslucentSpriteSystem() { unsetSingleton(); }

ID<Component> TranslucentSpriteSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void TranslucentSpriteSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<TranslucentSpriteComponent>(instance));
	destroyResources(View<SpriteRenderComponent>(componentView));
	components.destroy(ID<TranslucentSpriteComponent>(instance));
}
const string& TranslucentSpriteSystem::getComponentName() const
{
	static const string name = "Translucent Sprite";
	return name;
}
type_index TranslucentSpriteSystem::getComponentType() const
{
	return typeid(TranslucentSpriteComponent);
}
View<Component> TranslucentSpriteSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<TranslucentSpriteComponent>(instance)));
}
void TranslucentSpriteSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

MeshRenderType TranslucentSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Translucent;
}
LinearPool<MeshRenderComponent>& TranslucentSpriteSystem::getMeshComponentPool()
{
	return *((LinearPool<MeshRenderComponent>*)&components);
}
psize TranslucentSpriteSystem::getMeshComponentSize() const
{
	return sizeof(TranslucentSpriteComponent);
}
LinearPool<SpriteRenderFrame>& TranslucentSpriteSystem::getFrameComponentPool()
{
	return *((LinearPool<SpriteRenderFrame>*)&animationFrames);
}
psize TranslucentSpriteSystem::getFrameComponentSize() const
{
	return sizeof(TranslucentSpriteFrame);
}

//**********************************************************************************************************************
ID<AnimationFrame> TranslucentSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	TranslucentSpriteFrame frame;
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);

	if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize || 
		frame.animateUvOffset || frame.animateColorMapLayer || frame.animateColorMap)
	{
		return ID<AnimationFrame>(animationFrames.create(frame));
	}

	return {};
}
View<AnimationFrame> TranslucentSpriteSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<TranslucentSpriteFrame>(frame)));
}
void TranslucentSpriteSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	auto frameView = animationFrames.get(ID<TranslucentSpriteFrame>(frame));
	destroyResources(View<SpriteRenderFrame>(frameView));
	animationFrames.destroy(ID<TranslucentSpriteFrame>(frame));
}
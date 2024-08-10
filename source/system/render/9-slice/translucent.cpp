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

#include "garden/system/render/9-slice/translucent.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
Translucent9SliceSystem::Translucent9SliceSystem(bool useDeferredBuffer, bool useLinearFilter) :
	NineSliceRenderSystem("9-slice/translucent", useDeferredBuffer, useLinearFilter) { }

ID<Component> Translucent9SliceSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void Translucent9SliceSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<Translucent9SliceComponent>(instance));
	destroyResources(View<SpriteRenderComponent>(componentView));
	components.destroy(ID<Translucent9SliceComponent>(instance));
}
const string& Translucent9SliceSystem::getComponentName() const
{
	static const string name = "Translucent 9-Slice";
	return name;
}
type_index Translucent9SliceSystem::getComponentType() const
{
	return typeid(Translucent9SliceComponent);
}
View<Component> Translucent9SliceSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<Translucent9SliceComponent>(instance)));
}
void Translucent9SliceSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

MeshRenderType Translucent9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Translucent;
}
LinearPool<MeshRenderComponent>& Translucent9SliceSystem::getMeshComponentPool()
{
	return *((LinearPool<MeshRenderComponent>*)&components);
}
psize Translucent9SliceSystem::getMeshComponentSize() const
{
	return sizeof(Translucent9SliceComponent);
}
LinearPool<SpriteRenderFrame>& Translucent9SliceSystem::getFrameComponentPool()
{
	return *((LinearPool<SpriteRenderFrame>*)&animationFrames);
}
psize Translucent9SliceSystem::getFrameComponentSize() const
{
	return sizeof(Translucent9SliceFrame);
}

//**********************************************************************************************************************
ID<AnimationFrame> Translucent9SliceSystem::deserializeAnimation(IDeserializer& deserializer)
{
	Translucent9SliceFrame frame;
	NineSliceRenderSystem::deserializeAnimation(deserializer, frame);

	if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize || 
		frame.animateUvOffset || frame.animateColorMapLayer || frame.animateColorMap ||
		frame.animateTextureBorder || frame.animateWindowBorder)
	{
		return ID<AnimationFrame>(animationFrames.create(frame));
	}

	return {};
}
View<AnimationFrame> Translucent9SliceSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<Translucent9SliceFrame>(frame)));
}
void Translucent9SliceSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	auto frameView = animationFrames.get(ID<Translucent9SliceFrame>(frame));
	destroyResources(View<SpriteRenderFrame>(frameView));
	animationFrames.destroy(ID<Translucent9SliceFrame>(frame));
}
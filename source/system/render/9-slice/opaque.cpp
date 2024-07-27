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

#include "garden/system/render/9-slice/opaque.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

Opaque9SliceSystem::Opaque9SliceSystem(bool useDeferredBuffer, bool useLinearFilter)
{
	this->deferredBuffer = useDeferredBuffer;
	this->linearFilter = useLinearFilter;
}

//**********************************************************************************************************************
ID<Component> Opaque9SliceSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void Opaque9SliceSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<Opaque9SliceComponent>(instance));
	tryDestroyResources(View<SpriteRenderComponent>(componentView));
	components.destroy(ID<Opaque9SliceComponent>(instance));
}
const string& Opaque9SliceSystem::getComponentName() const
{
	static const string name = "Opaque 9-Slice";
	return name;
}
type_index Opaque9SliceSystem::getComponentType() const
{
	return typeid(Opaque9SliceComponent);
}
View<Component> Opaque9SliceSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<Opaque9SliceComponent>(instance)));
}
void Opaque9SliceSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}

//**********************************************************************************************************************
MeshRenderType Opaque9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
const LinearPool<MeshRenderComponent>& Opaque9SliceSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize Opaque9SliceSystem::getMeshComponentSize() const
{
	return sizeof(Opaque9SliceComponent);
}

ID<GraphicsPipeline> Opaque9SliceSystem::createPipeline()
{
	ID<Framebuffer> framebuffer;
	if (deferredBuffer)
		framebuffer = DeferredRenderSystem::getInstance()->getGFramebuffer();
	else
		framebuffer = ForwardRenderSystem::getInstance()->getFramebuffer();

	map<string, GraphicsPipeline::SamplerState> samplerStateOverrides;
	if (!linearFilter)
	{
		GraphicsPipeline::SamplerState samplerState;
		samplerState.wrapX = samplerState.wrapY = samplerState.wrapZ =
			GraphicsPipeline::SamplerWrap::Repeat;
		samplerStateOverrides.emplace("colorMap", samplerState);
	}

	return ResourceSystem::getInstance()->loadGraphicsPipeline("9-slice/opaque",
		framebuffer, true, true, 0, 0, {}, samplerStateOverrides, {});
}

//**********************************************************************************************************************
ID<AnimationFrame> Opaque9SliceSystem::deserializeAnimation(IDeserializer& deserializer)
{
	Opaque9SliceFrame frame;
	NineSliceRenderSystem::deserializeAnimation(deserializer, frame);

	if (frame.animateIsEnabled || frame.animateColorFactor || frame.animateUvSize || frame.animateUvOffset || 
		frame.animateColorMapLayer || frame.animateTextureBorder || frame.animateWindowBorder)
	{
		return ID<AnimationFrame>(animationFrames.create(frame));
	}

	return {};
}
View<AnimationFrame> Opaque9SliceSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<Opaque9SliceFrame>(frame)));
}
void Opaque9SliceSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<Opaque9SliceFrame>(frame));
}
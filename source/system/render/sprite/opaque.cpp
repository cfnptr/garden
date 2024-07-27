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

OpaqueSpriteSystem::OpaqueSpriteSystem(bool useDeferredBuffer, bool useLinearFilter)
{
	this->deferredBuffer = useDeferredBuffer;
	this->linearFilter = useLinearFilter;
}

//**********************************************************************************************************************
ID<Component> OpaqueSpriteSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void OpaqueSpriteSystem::destroyComponent(ID<Component> instance)
{
	auto componentView = components.get(ID<OpaqueSpriteComponent>(instance));
	tryDestroyResources(View<SpriteRenderComponent>(componentView));
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

//**********************************************************************************************************************
MeshRenderType OpaqueSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
const LinearPool<MeshRenderComponent>& OpaqueSpriteSystem::getMeshComponentPool() const
{
	return *((const LinearPool<MeshRenderComponent>*)&components);
}
psize OpaqueSpriteSystem::getMeshComponentSize() const
{
	return sizeof(OpaqueSpriteComponent);
}

ID<GraphicsPipeline> OpaqueSpriteSystem::createPipeline()
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

	return ResourceSystem::getInstance()->loadGraphicsPipeline("sprite/opaque",
		framebuffer, true, true, 0, 0, {}, samplerStateOverrides, {});
}

//**********************************************************************************************************************
ID<AnimationFrame> OpaqueSpriteSystem::deserializeAnimation(IDeserializer& deserializer)
{
	OpaqueSpriteFrame frame;
	SpriteRenderSystem::deserializeAnimation(deserializer, frame);

	if (frame.animateIsEnabled || frame.animateColorFactor ||
		frame.animateUvSize || frame.animateUvOffset || frame.animateColorMapLayer)
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
	animationFrames.destroy(ID<OpaqueSpriteFrame>(frame));
}
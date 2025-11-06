// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
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

#include "garden/system/ui/transform.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/profiler.hpp"

using namespace garden;

//**********************************************************************************************************************
UiTransformSystem::UiTransformSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);

	ECSM_SUBSCRIBE_TO_EVENT("Update", UiTransformSystem::update);
}
UiTransformSystem::~UiTransformSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", UiTransformSystem::update);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
static void transformUiComponent(Manager* manager, float2 uiHalfSize, UiTransformComponent& uiTransformComp)
{
	auto entity = uiTransformComp.getEntity();
	if (!entity)
		return;

	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView || !transformView->isActive())
		return;

	auto position = (float2)uiTransformComp.position;
	auto scale = (float2)uiTransformComp.scale;

	switch (uiTransformComp.anchor)
	{
		case UiAnchor::Center: break;
		case UiAnchor::Left: position.x -= uiHalfSize.x; break;
		case UiAnchor::Right: position.x += uiHalfSize.x; break;
		case UiAnchor::Bottom: position.y -= uiHalfSize.y; break;
		case UiAnchor::Top: position.y += uiHalfSize.y; break;
		case UiAnchor::LeftBottom: position -= uiHalfSize; break;
		case UiAnchor::LeftTop: position += float2(-uiHalfSize.x, uiHalfSize.y); break;
		case UiAnchor::RightBottom: position += float2(uiHalfSize.x, -uiHalfSize.y); break;
		case UiAnchor::RightTop: position += uiHalfSize; break;
		case UiAnchor::Background:
			scale *= uiHalfSize.x / uiHalfSize.y > scale.x / scale.y ?
				(uiHalfSize.x * 2.0f) / scale.x : (uiHalfSize.y * 2.0f) / scale.y;
			break;
		default: abort();
	}

	transformView->setPosition(float3(position, uiTransformComp.position.getZ()));
	transformView->setScale(float3(scale, uiTransformComp.scale.getZ()));
	transformView->setRotation(uiTransformComp.rotation);
}

void UiTransformSystem::update()
{
	SET_CPU_ZONE_SCOPED("UI Transform Update");

	auto inputSystem = InputSystem::Instance::get();
	uiSize = (float2)inputSystem->getWindowSize() * inputSystem->getContentScale() * uiScale;
	cursorPosition = (inputSystem->getCursorPosition() - (float2)inputSystem->getWindowSize() * 0.5f) * uiScale;
	// TODO: take into account macOS differend window and framebuffer scale!

	if (components.getCount() == 0)
		return;

	auto componentData = components.getData();
	auto threadSystem = ThreadSystem::Instance::tryGet();
	auto uiHalfSize = uiSize * 0.5f;

	if (threadSystem && components.getCount() > threadSystem->getForegroundPool().getThreadCount())
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([componentData, uiHalfSize](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("UI Transform Update");

			auto itemCount = task.getItemCount();
			auto manager = Manager::Instance::get();

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				transformUiComponent(manager, uiHalfSize, componentData[i]);
		},
		components.getOccupancy());
		threadPool.wait();
	}
	else
	{
		auto componentOccupancy = components.getOccupancy();
		auto manager = Manager::Instance::get();

		for (uint32 i = 0; i < componentOccupancy; i++)
			transformUiComponent(manager, uiHalfSize, componentData[i]);
	}
}

string_view UiTransformSystem::getComponentName() const
{
	return "Transform UI";
}

//**********************************************************************************************************************
void UiTransformSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiTransformComponent>(component);
	if (componentView->position != f32x4::zero)
		serializer.write("position", (float3)componentView->position);
	if (componentView->scale != f32x4::one)
		serializer.write("scale", (float3)componentView->scale);
	if (componentView->rotation != quat::identity)
		serializer.write("rotation", componentView->rotation);
	if (componentView->anchor != UiAnchor::Center)
		serializer.write("anchor", toString(componentView->anchor));
}
void UiTransformSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiTransformComponent>(component);
	deserializer.read("position", componentView->position);
	deserializer.read("scale", componentView->scale);
	deserializer.read("rotation", componentView->rotation);

	string anchor;
	if (deserializer.read("anchor", anchor))
		toUiAnchor(anchor, componentView->anchor);
}

//**********************************************************************************************************************
void UiTransformSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiTransformFrame>(frame);
	if (frameView->animatePosition)
		serializer.write("position", (float3)frameView->position);
	if (frameView->animateScale)
		serializer.write("scale", (float3)frameView->scale);
	if (frameView->animateRotation)
		serializer.write("rotation", frameView->rotation);
	if (frameView->animateAnchor)
		serializer.write("anchor", toString(frameView->anchor));
}
void UiTransformSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiTransformFrame>(frame); string anchor;
	frameView->animatePosition = deserializer.read("position", frameView->position, 3);
	frameView->animateScale = deserializer.read("scale", frameView->scale, 3);
	frameView->animateRotation = deserializer.read("rotation", frameView->rotation);
	frameView->animateAnchor = deserializer.read("rotation", anchor);

	if (frameView->animateAnchor)
		toUiAnchor(anchor, frameView->anchor);
}

void UiTransformSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentiew = View<UiTransformComponent>(component);
	const auto frameA = View<UiTransformFrame>(a);
	const auto frameB = View<UiTransformFrame>(b);

	if (frameA->animatePosition)
		componentiew->position = lerp(frameA->position, frameB->position, t);
	if (frameA->animateScale)
		componentiew->scale = lerp(frameA->scale, frameB->scale, t);
	if (frameA->animateRotation)
		componentiew->rotation = slerp(frameA->rotation, frameB->rotation, t);
	if (frameA->animateAnchor)
		componentiew->anchor = (bool)round(t) ? frameB->anchor : frameA->anchor;
}
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
static void transformUiComponent(float2 uiHalfSize, UiTransformComponent& uiTransformComp)
{
	auto entity = uiTransformComp.getEntity();
	if (!entity)
		return;

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(entity);
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

	transformView->setPosition(float3(position, uiTransformComp.position.z));
	transformView->setScale(float3(scale, uiTransformComp.scale.z));
	transformView->setRotation(uiTransformComp.rotation);
}

void UiTransformSystem::update()
{
	SET_CPU_ZONE_SCOPED("UI Transform Update");

	if (components.getCount() == 0)
		return;

	auto componentData = components.getData();
	auto threadSystem = ThreadSystem::Instance::tryGet();
	auto uiHalfSize = calcUiSize() * 0.5f;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([componentData, uiHalfSize](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("UI Transform Update");

			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				transformUiComponent(uiHalfSize, componentData[i]);
		},
		components.getOccupancy());
		threadPool.wait();
	}
	else
	{
		auto componentOccupancy = components.getOccupancy();
		for (uint32 i = 0; i < componentOccupancy; i++)
			transformUiComponent(uiHalfSize, componentData[i]);
	}
}

void UiTransformSystem::resetComponent(View<Component> component, bool full)
{
	if (full)
	{
		auto uiTransformView = View<UiTransformComponent>(component);
		uiTransformView->position = float3::zero;
		uiTransformView->scale = float3::one;
		uiTransformView->anchor = UiAnchor::Center;
		uiTransformView->rotation = quat::identity;
	}
}
void UiTransformSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiTransformComponent>(source);
	auto destinationView = View<UiTransformComponent>(destination);
	destinationView->position = sourceView->position;
	destinationView->scale = sourceView->scale;
	destinationView->anchor = sourceView->anchor;
	destinationView->rotation = sourceView->rotation;
}
string_view UiTransformSystem::getComponentName() const
{
	return "Transform UI";
}

//**********************************************************************************************************************
void UiTransformSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto uiTransformView = View<UiTransformComponent>(component);
	if (uiTransformView->position != float3::zero)
		serializer.write("position", uiTransformView->position);
	if (uiTransformView->scale != float3::one)
		serializer.write("scale", uiTransformView->scale);
	if (uiTransformView->rotation != quat::identity)
		serializer.write("rotation", uiTransformView->rotation);

	switch (uiTransformView->anchor)
	{
		case UiAnchor::Center: break;
		case UiAnchor::Left: serializer.write("anchor", string_view("Left")); break;
		case UiAnchor::Right: serializer.write("anchor", string_view("Right")); break;
		case UiAnchor::Bottom: serializer.write("anchor", string_view("Bottom")); break;
		case UiAnchor::Top: serializer.write("anchor", string_view("Top")); break;
		case UiAnchor::LeftBottom: serializer.write("anchor", string_view("LeftBottom")); break;
		case UiAnchor::LeftTop: serializer.write("anchor", string_view("LeftTop")); break;
		case UiAnchor::RightBottom: serializer.write("anchor", string_view("RightBottom")); break;
		case UiAnchor::RightTop: serializer.write("anchor", string_view("RightTop")); break;
		case UiAnchor::Background: serializer.write("anchor", string_view("Background")); break;
		default: abort();
	}
}
void UiTransformSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto uiTransformView = View<UiTransformComponent>(component);
	deserializer.read("position", uiTransformView->position);
	deserializer.read("scale", uiTransformView->scale);
	deserializer.read("rotation", uiTransformView->rotation);

	string anchor; deserializer.read("anchor", anchor);
	if (anchor == "Left") uiTransformView->anchor = UiAnchor::Left;
	else if (anchor == "Right") uiTransformView->anchor = UiAnchor::Right;
	else if (anchor == "Bottom") uiTransformView->anchor = UiAnchor::Bottom;
	else if (anchor == "Top") uiTransformView->anchor = UiAnchor::Top;
	else if (anchor == "LeftBottom") uiTransformView->anchor = UiAnchor::LeftBottom;
	else if (anchor == "LeftTop") uiTransformView->anchor = UiAnchor::LeftTop;
	else if (anchor == "RightBottom") uiTransformView->anchor = UiAnchor::RightBottom;
	else if (anchor == "RightTop") uiTransformView->anchor = UiAnchor::RightTop;
	else if (anchor == "Background") uiTransformView->anchor = UiAnchor::Background;
}

//**********************************************************************************************************************
void UiTransformSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto uiTransformFrameView = View<UiTransformFrame>(frame);
	if (uiTransformFrameView->animatePosition)
		serializer.write("position", (float3)uiTransformFrameView->position);
	if (uiTransformFrameView->animateScale)
		serializer.write("scale", (float3)uiTransformFrameView->scale);
	if (uiTransformFrameView->animateRotation)
		serializer.write("rotation", uiTransformFrameView->rotation);
}
ID<AnimationFrame> UiTransformSystem::deserializeAnimation(IDeserializer& deserializer)
{
	UiTransformFrame frame;
	frame.animatePosition = deserializer.read("position", frame.position, 3);
	frame.animateScale = deserializer.read("scale", frame.scale, 3);
	frame.animateRotation = deserializer.read("rotation", frame.rotation);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}

void UiTransformSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto uiTransformView = View<UiTransformComponent>(component);
	const auto frameA = View<UiTransformFrame>(a);
	const auto frameB = View<UiTransformFrame>(b);

	if (frameA->animatePosition)
		uiTransformView->position = (float3)lerp(frameA->position, frameB->position, t);
	if (frameA->animateScale)
		uiTransformView->scale = (float3)lerp(frameA->scale, frameB->scale, t);
	if (frameA->animateRotation)
		uiTransformView->rotation = slerp(frameA->rotation, frameB->rotation, t);
}

float2 UiTransformSystem::calcUiSize() const
{
	auto inputSystem = InputSystem::Instance::get();
	return (float2)inputSystem->getWindowSize() * inputSystem->getContentScale() * uiScale;
}
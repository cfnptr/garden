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

#include "garden/system/ui/button.hpp"

using namespace garden;

//**********************************************************************************************************************
UiButtonSystem::UiButtonSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
}
UiButtonSystem::~UiButtonSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
	}
	unsetSingleton();
}

void UiButtonSystem::resetComponent(View<Component> component, bool full)
{
	if (full)
	{
		auto uiButtonView = View<UiButtonComponent>(component);
		uiButtonView->isEnabled = true;
	}
}
void UiButtonSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiButtonComponent>(source);
	auto destinationView = View<UiButtonComponent>(destination);
	destinationView->isEnabled = sourceView->isEnabled;
}
string_view UiButtonSystem::getComponentName() const
{
	return "Button UI";
}

//**********************************************************************************************************************
void UiButtonSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto uiButtonView = View<UiButtonComponent>(component);
	if (uiButtonView->isEnabled != true)
		serializer.write("isEnabled", uiButtonView->isEnabled);
}
void UiButtonSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto uiButtonView = View<UiButtonComponent>(component);
	deserializer.read("isEnabled", uiButtonView->isEnabled);
}

//**********************************************************************************************************************
void UiButtonSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto uiButtonFrameView = View<UiButtonFrame>(frame);
	if (uiButtonFrameView->animateIsEnabled)
		serializer.write("isEnabled", (float3)uiButtonFrameView->isEnabled);
}
ID<AnimationFrame> UiButtonSystem::deserializeAnimation(IDeserializer& deserializer)
{
	UiButtonFrame frame;
	frame.animateIsEnabled = deserializer.read("isEnabled", frame.isEnabled);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}

void UiButtonSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto uiButtonView = View<UiButtonComponent>(component);
	const auto frameA = View<UiButtonFrame>(a);
	const auto frameB = View<UiButtonFrame>(b);

	if (frameA->animateIsEnabled)
		uiButtonView->isEnabled = (bool)round(t);
}
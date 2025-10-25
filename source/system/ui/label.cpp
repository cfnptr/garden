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

#include "garden/system/ui/label.hpp"

using namespace garden;

void UiLabelComponent::setValue(string_view value)
{
	if (value == this->value)
		return;

	abort(); // TODO: update text atlas.
	this->value = value;
}

UiLabelSystem::UiLabelSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
}
UiLabelSystem::~UiLabelSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiLabelSystem::resetComponent(View<Component> component, bool full)
{
	auto uiLabelView = View<UiLabelComponent>(component);
	TextSystem::Instance::get()->destroy(uiLabelView->text);
	uiLabelView->text = {};

	if (full)
		uiLabelView->value = "";
}
void UiLabelSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiLabelComponent>(source);
	auto destinationView = View<UiLabelComponent>(destination);
	destinationView->value = sourceView->value;
	abort(); // TODO: duplicate text object
}
string_view UiLabelSystem::getComponentName() const
{
	return "Label UI";
}

void UiLabelSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto uiLabelView = View<UiLabelComponent>(component);
	if (!uiLabelView->value.empty())
		serializer.write("value", uiLabelView->value);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!uiLabelView->fontPath.empty())
		serializer.write("fontPath", uiLabelView->fontPath.generic_string());
	if (uiLabelView->taskPriority != 0.0f)
		serializer.write("taskPriority", uiLabelView->taskPriority);
	#endif
}
void UiLabelSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto uiLabelView = View<UiLabelComponent>(component);
	deserializer.read("value", uiLabelView->value);

	string fontPath;
	deserializer.read("fontPath", fontPath);
	if (fontPath.empty())
		fontPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	uiLabelView->fontPath = fontPath;
	#endif

	float taskPriority = 0.0f;
	deserializer.read("taskPriority", taskPriority);

	abort(); // TODO: load fonts and text
}

//**********************************************************************************************************************
void UiLabelSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto uiLabelFrameView = View<UiLabelFrame>(frame);
	if (uiLabelFrameView->animateValue)
	{
		serializer.write("value", uiLabelFrameView->value);

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (!uiLabelFrameView->fontPath.empty())
			serializer.write("fontPath", uiLabelFrameView->fontPath.generic_string());
		if (uiLabelFrameView->taskPriority != 0.0f)
			serializer.write("taskPriority", uiLabelFrameView->taskPriority);
		#endif
	}
}
ID<AnimationFrame> UiLabelSystem::deserializeAnimation(IDeserializer& deserializer)
{
	UiLabelFrame frame;
	frame.animateValue = deserializer.read("value", frame.value);

	string fontPath;
	deserializer.read("fontPath", fontPath);
	if (fontPath.empty())
		fontPath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.fontPath = fontPath;
	#endif

	abort(); // TODO: load fonts and text

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}

void UiLabelSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto uiLabelView = View<UiLabelComponent>(component);
	const auto frameA = View<UiLabelFrame>(a);
	const auto frameB = View<UiLabelFrame>(b);

	if (frameA->animateValue)
	{
		if (round(t) > 0.0f)
		{
			if (frameB->text)
			{
				uiLabelView->value = frameB->value;
				uiLabelView->text = frameB->text;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				uiLabelView->fontPath = frameB->fontPath;
				#endif
			}
		}
		else
		{
			if (frameA->text)
			{
				uiLabelView->value = frameA->value;
				uiLabelView->text = frameA->text;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				uiLabelView->fontPath = frameA->fontPath;
				#endif
			}
		}
	}
}

void UiLabelSystem::resetAnimation(View<AnimationFrame> frame, bool full)
{
	auto uiLabelFrameView = View<UiLabelFrame>(frame);
	TextSystem::Instance::get()->destroy(uiLabelFrameView->text);
	uiLabelFrameView->text = {};
}
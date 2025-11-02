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

#include "garden/system/ui/checkbox.hpp"
#include "garden/system/ui/trigger.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/input.hpp"

using namespace garden;

void UiCheckboxComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	auto uiButtonView = Manager::Instance::get()->tryGet<UiButtonComponent>(entity);
	if (uiButtonView)
		uiButtonView->setEnabled(state);
	
	enabled = state;
}
void UiCheckboxComponent::setChecked(bool state)
{
	if (checked == state)
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->get<TransformComponent>(entity);
	auto checkmark = transformView->tryGetChild(UiCheckboxSystem::checkmarkChildIndex);
	if (checkmark)
	{
		transformView = manager->get<TransformComponent>(checkmark);
		transformView->setActive(state);
	}

	checked = state;
}

UiCheckboxSystem::UiCheckboxSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);

	manager->registerEvent("UiCheckboxClick");
	ECSM_SUBSCRIBE_TO_EVENT("UiCheckboxClick", UiCheckboxSystem::uiCheckboxClick);
}
UiCheckboxSystem::~UiCheckboxSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);

		manager->unregisterEvent("UiCheckboxClick");
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiCheckboxClick", UiCheckboxSystem::uiCheckboxClick);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiCheckboxSystem::uiCheckboxClick()
{
	auto manager = Manager::Instance::get();
	auto hoveredButton = UiTriggerSystem::Instance::get()->getHovered();
	auto uiCheckboxView = manager->tryGet<UiCheckboxComponent>(hoveredButton);
	if (!uiCheckboxView)
		return;

	uiCheckboxView->setChecked(!uiCheckboxView->checked);

	if (!uiCheckboxView->onChange.empty())
		manager->tryRunEvent(uiCheckboxView->onChange);
}

//**********************************************************************************************************************
string_view UiCheckboxSystem::getComponentName() const
{
	return "Checkbox UI";
}

void UiCheckboxSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiCheckboxComponent>(component);
	if (componentView->enabled != true)
		serializer.write("isEnabled", componentView->enabled);
	if (componentView->checked != false)
		serializer.write("isChecked", componentView->checked);
	if (!componentView->onChange.empty())
		serializer.write("onChange", componentView->onChange);
}
void UiCheckboxSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiCheckboxComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("isChecked", componentView->checked);
	deserializer.read("onChange", componentView->onChange);
}

//**********************************************************************************************************************
void UiCheckboxSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiCheckboxFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", frameView->isEnabled);
	if (frameView->animateIsChecked)
		serializer.write("isChecked", frameView->isChecked);
	if (frameView->animateOnChange)
		serializer.write("onChange", frameView->onChange);
}
void UiCheckboxSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiCheckboxFrame>(frame); auto boolValue = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frameView->isEnabled = boolValue;

	boolValue = false;
	frameView->animateIsChecked = deserializer.read("isChecked", boolValue);
	frameView->isChecked = boolValue;

	frameView->animateOnChange = deserializer.read("onChange", frameView->onChange);
}

void UiCheckboxSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiCheckboxComponent>(component);
	const auto frameA = View<UiCheckboxFrame>(a);
	const auto frameB = View<UiCheckboxFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->enabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateIsChecked)
		componentView->checked = (bool)round(t) ? frameB->isChecked : frameA->isChecked;
	if (frameA->animateOnChange)
		componentView->onChange = (bool)round(t) ? frameB->onChange : frameA->onChange;
}
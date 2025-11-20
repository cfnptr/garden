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
#include "garden/system/animation.hpp"

using namespace garden;

static void setUiCheckboxAnimation(ID<Entity> element, string_view animationPath, bool state)
{
	if (animationPath.empty())
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(element);
	if (!transformView)
		return;
	auto checkmark = transformView->tryGetChild(0);
	if (!checkmark)
		return;
	auto animationView = manager->tryGet<AnimationComponent>(checkmark);
	if (!animationView)
		return;

	animationView->active = animationPath; animationView->active.push_back('/');
	animationView->active += state ? "set" : "unset";
	animationView->frame = 0.0f;
	animationView->isPlaying = true;
}

void UiCheckboxComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	auto uiCheckboxView = Manager::Instance::get()->tryGet<UiButtonComponent>(entity);
	if (uiCheckboxView)
		uiCheckboxView->setEnabled(state);
	enabled = state;
}
void UiCheckboxComponent::setChecked(bool state)
{
	if (checked == state)
		return;

	setUiCheckboxAnimation(entity, animationPath, state);
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

		ECSM_UNSUBSCRIBE_FROM_EVENT("UiCheckboxClick", UiCheckboxSystem::uiCheckboxClick);
		manager->unregisterEvent("UiCheckboxClick");
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiCheckboxSystem::uiCheckboxClick()
{
	auto manager = Manager::Instance::get();
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiCheckboxView = manager->tryGet<UiCheckboxComponent>(hoveredElement);
	if (!uiCheckboxView)
		return;

	uiCheckboxView->setChecked(!uiCheckboxView->checked);

	if (!uiCheckboxView->onChange.empty())
		manager->tryRunEvent(uiCheckboxView->onChange);
}

string_view UiCheckboxSystem::getComponentName() const
{
	return "Checkbox UI";
}

void UiCheckboxSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiCheckboxComponent>(component);
	if (!componentView->enabled)
		serializer.write("isEnabled", false);
	if (componentView->checked)
		serializer.write("isChecked", true);
	if (!componentView->onChange.empty())
		serializer.write("onChange", componentView->onChange);
	if (!componentView->animationPath.empty())
		serializer.write("animationPath", componentView->animationPath);
}
void UiCheckboxSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiCheckboxComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("isChecked", componentView->checked);
	deserializer.read("onChange", componentView->onChange);
	deserializer.read("animationPath", componentView->animationPath);
}

//**********************************************************************************************************************
void UiCheckboxSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiCheckboxFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
	if (frameView->animateIsChecked)
		serializer.write("isChecked", (bool)frameView->isChecked);
	if (frameView->animateOnChange)
		serializer.write("onChange", frameView->onChange);
	if (frameView->animateAnimationPath)
		serializer.write("animationPath", frameView->animationPath);
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
	frameView->animateAnimationPath = deserializer.read("animationPath", frameView->animationPath);
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
	if (frameA->animateAnimationPath)
		componentView->animationPath = (bool)round(t) ? frameB->animationPath : frameA->animationPath;
}
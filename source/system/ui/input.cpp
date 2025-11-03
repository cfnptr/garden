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

#include "garden/system/ui/input.hpp"
#include "garden/system/ui/trigger.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/input.hpp"

using namespace garden;

static void setUiInputAnimation(ID<Entity> uiButton, string_view animationPath, string_view state)
{
	if (animationPath.empty())
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(uiButton);
	if (!transformView)
		return;

	auto panel = transformView->tryGetChild(0);
	if (!panel)
		return;
	auto animationView = manager->tryGet<AnimationComponent>(panel);
	if (!animationView)
		return;

	animationView->active = animationPath; animationView->active += "/";
	animationView->active += state;

	animationView->frame = 0.0f;
	animationView->isPlaying = true;
}

void UiInputComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	setUiInputAnimation(entity, animationPath, state ? "default" : "disabled");
	enabled = state;
}

UiInputSystem::UiInputSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);

	manager->registerEvent("UiInputEnter");
	manager->registerEvent("UiInputExit");
	manager->registerEvent("UiInputStay");

	ECSM_SUBSCRIBE_TO_EVENT("UiInputEnter", UiInputSystem::uiInputEnter);
	ECSM_SUBSCRIBE_TO_EVENT("UiInputExit", UiInputSystem::uiInputExit);
	ECSM_SUBSCRIBE_TO_EVENT("UiInputStay", UiInputSystem::uiInputStay);
	ECSM_SUBSCRIBE_TO_EVENT("Update", UiInputSystem::update);
}
UiInputSystem::~UiInputSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);

		manager->unregisterEvent("UiInputEnter");
		manager->unregisterEvent("UiInputExit");
		manager->unregisterEvent("UiInputStay");

		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputEnter", UiInputSystem::uiInputEnter);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputExit", UiInputSystem::uiInputExit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputStay", UiInputSystem::uiInputStay);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", UiInputSystem::update);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiInputSystem::uiInputEnter()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled)
		return;

	if (hoveredElement != activeInput)
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, "hovered");
	InputSystem::Instance::get()->setCursorType(CursorType::Ibeam);
}
void UiInputSystem::uiInputExit()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled)
		return;

	if (hoveredElement != activeInput)
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, "default");
	InputSystem::Instance::get()->setCursorType(CursorType::Default);
}
void UiInputSystem::uiInputStay()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled || hoveredElement == activeInput)
		return;

	if (InputSystem::Instance::get()->isMousePressed(MouseButton::Left))
	{
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, "active");
		activeInput = hoveredElement;
	}
}

void UiInputSystem::update()
{
	if (activeInput)
	{
		auto inputSystem = InputSystem::Instance::get();
		auto deactivateInput = false;

		if (inputSystem->getCursorMode() != CursorMode::Normal)
			deactivateInput = true;
		else if (inputSystem->isMousePressed(MouseButton::Left))
		{
			auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
			deactivateInput = activeInput != hoveredElement;
		}

		if (deactivateInput)
		{
			auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(activeInput);
			if (uiInputView)
				setUiInputAnimation(activeInput, uiInputView->animationPath, "default");
			activeInput = {};
		}
		else
		{
			// TODO: update input
		}
	}
}

//**********************************************************************************************************************
string_view UiInputSystem::getComponentName() const
{
	return "Input UI";
}

void UiInputSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiInputComponent>(component);
	if (componentView->enabled != true)
		serializer.write("isEnabled", componentView->enabled);
	if (!componentView->onChange.empty())
		serializer.write("onChange", componentView->onChange);
	if (!componentView->animationPath.empty())
		serializer.write("animationPath", componentView->animationPath);
}
void UiInputSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiInputComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("onChange", componentView->onChange);
	deserializer.read("animationPath", componentView->animationPath);
}

//**********************************************************************************************************************
void UiInputSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiInputFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", frameView->isEnabled);
	if (frameView->animateOnChange)
		serializer.write("onChange", frameView->onChange);
	if (frameView->animateAnimationPath)
		serializer.write("animationPath", frameView->animationPath);
}
void UiInputSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiInputFrame>(frame); auto isEnabled = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", isEnabled);
	frameView->animateOnChange = deserializer.read("onChange", frameView->onChange);
	frameView->animateAnimationPath = deserializer.read("animationPath", frameView->animationPath);
	frameView->isEnabled = isEnabled;
}

void UiInputSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiInputComponent>(component);
	const auto frameA = View<UiInputFrame>(a);
	const auto frameB = View<UiInputFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->enabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateOnChange)
		componentView->onChange = (bool)round(t) ? frameB->onChange : frameA->onChange;
	if (frameA->animateAnimationPath)
		componentView->animationPath = (bool)round(t) ? frameB->animationPath : frameA->animationPath;
}
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
#include "garden/system/ui/trigger.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/input.hpp"

using namespace garden;

static void activateButtonState(ID<Entity> button, uint8 childIndex)
{
	auto transformSystem = TransformSystem::Instance::get();
	auto transformView = transformSystem->getComponent(button);
	auto childCount = min((uint8)transformView->getChildCount(), UiButtonSystem::stateChildCount);
	auto childs = transformView->getChilds();

	for (uint8 i = 0; i < childCount; i++)
	{
		transformView = transformSystem->getComponent(childs[i]);
		transformView->setActive(i == childIndex);
	}
}

void UiButtonComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	auto childIndex = state ? UiButtonSystem::defaultChildIndex : 
		UiButtonSystem::disabledChildIndex;
	activateButtonState(entity, childIndex);
	enabled = state;
}

UiButtonSystem::UiButtonSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);

	manager->registerEvent("UiButtonEnter");
	manager->registerEvent("UiButtonExit");
	manager->registerEvent("UiButtonStay");

	ECSM_SUBSCRIBE_TO_EVENT("UiButtonEnter", UiButtonSystem::uiButtonEnter);
	ECSM_SUBSCRIBE_TO_EVENT("UiButtonExit", UiButtonSystem::uiButtonExit);
	ECSM_SUBSCRIBE_TO_EVENT("UiButtonStay", UiButtonSystem::uiButtonStay);
}
UiButtonSystem::~UiButtonSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);

		manager->unregisterEvent("UiButtonEnter");
		manager->unregisterEvent("UiButtonExit");
		manager->unregisterEvent("UiButtonStay");

		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonEnter", UiButtonSystem::uiButtonEnter);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonExit", UiButtonSystem::uiButtonExit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonStay", UiButtonSystem::uiButtonStay);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiButtonSystem::uiButtonEnter()
{
	auto hoveredButton = UiTriggerSystem::Instance::get()->getHovered();
	auto uiButtonView = tryGetComponent(hoveredButton);
	if (!uiButtonView || !uiButtonView->enabled)
		return;

	auto mouseState = InputSystem::Instance::get()->getMouseState(MouseButton::Left);
	auto childIndex = pressedButton == hoveredButton && mouseState ? activeChildIndex : hoveredChildIndex;
	activateButtonState(hoveredButton, childIndex);
}
void UiButtonSystem::uiButtonExit()
{
	auto hoveredButton = UiTriggerSystem::Instance::get()->getHovered();
	auto uiButtonView = tryGetComponent(hoveredButton);
	if (!uiButtonView)
		return;

	auto childIndex = uiButtonView->enabled ? defaultChildIndex : disabledChildIndex;
	activateButtonState(hoveredButton, childIndex);
}
void UiButtonSystem::uiButtonStay()
{
	auto hoveredButton = UiTriggerSystem::Instance::get()->getHovered();
	auto uiButtonView = tryGetComponent(hoveredButton);
	if (!uiButtonView || !uiButtonView->enabled)
		return;

	auto inputSystem = InputSystem::Instance::get();
	if (inputSystem->isMousePressed(MouseButton::Left))
	{
		activateButtonState(hoveredButton, activeChildIndex);
		pressedButton = hoveredButton;
	}
	else if (inputSystem->isMouseReleased(MouseButton::Left))
	{
		if (pressedButton == hoveredButton)
		{
			activateButtonState(hoveredButton, hoveredChildIndex);
			if (!uiButtonView->onClick.empty())
				Manager::Instance::get()->tryRunEvent(uiButtonView->onClick);
		}
		pressedButton = {};
	}
}

//**********************************************************************************************************************
string_view UiButtonSystem::getComponentName() const
{
	return "Button UI";
}

void UiButtonSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiButtonComponent>(component);
	if (componentView->enabled != true)
		serializer.write("isEnabled", componentView->enabled);
	if (!componentView->onClick.empty())
		serializer.write("onClick", componentView->onClick);
}
void UiButtonSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiButtonComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("onClick", componentView->onClick);
}

//**********************************************************************************************************************
void UiButtonSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiButtonFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", frameView->isEnabled);
	if (frameView->animateOnClick)
		serializer.write("onClick", frameView->onClick);
}
void UiButtonSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiButtonFrame>(frame);
	frameView->animateIsEnabled = deserializer.read("isEnabled", frameView->isEnabled);
	frameView->animateOnClick = deserializer.read("onClick", frameView->onClick);
}

void UiButtonSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiButtonComponent>(component);
	const auto frameA = View<UiButtonFrame>(a);
	const auto frameB = View<UiButtonFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->enabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateOnClick)
		componentView->onClick = (bool)round(t) ? frameB->onClick : frameA->onClick;
}
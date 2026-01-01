// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/animation.hpp"
#include "garden/system/input.hpp"

using namespace garden;

static void setUiButtonAnimation(ID<Entity> element, string_view animationPath, string_view currState, string_view newState)
{
	if (animationPath.empty())
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(element);
	auto animationView = manager->tryGet<AnimationComponent>(element);
	if (!transformView || !animationView)
		return;

	auto transState = string(animationPath); transState.push_back('/');
	transState += currState; transState.push_back('-'); transState += newState;
	if (animationView->hasAnimation(transState))
	{
		animationView->active = std::move(transState);
	}
	else
	{
		animationView->active = animationPath; animationView->active.push_back('/');
		animationView->active += newState;
	}

	animationView->frame = 0.0f;
	animationView->isPlaying = true;
}

void UiButtonComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	auto currState = hoveredElement == entity ? "hovered" : "default";
	setUiButtonAnimation(entity, animationPath, enabled ? 
		currState : "disabled", state ? currState : "disabled");

	if (!noCursorHand && hoveredElement == entity)
	{
		InputSystem::Instance::get()->setCursorType(
			state ? CursorType::PointingHand : CursorType::Default);
	}
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

		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonEnter", UiButtonSystem::uiButtonEnter);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonExit", UiButtonSystem::uiButtonExit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiButtonStay", UiButtonSystem::uiButtonStay);

		manager->unregisterEvent("UiButtonEnter");
		manager->unregisterEvent("UiButtonExit");
		manager->unregisterEvent("UiButtonStay");
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiButtonSystem::uiButtonEnter()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiButtonView = Manager::Instance::get()->tryGet<UiButtonComponent>(hoveredElement);
	if (!uiButtonView || !uiButtonView->enabled)
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto mouseState = inputSystem->getMouseState(MouseButton::Left);
	auto newState = pressedButton == hoveredElement && mouseState ? "active" : "hovered";
	setUiButtonAnimation(hoveredElement, uiButtonView->animationPath, "default", newState);

	if (!uiButtonView->noCursorHand)
		inputSystem->setCursorType(CursorType::PointingHand);
	if (!mouseState)
		pressedButton = {};
}
void UiButtonSystem::uiButtonExit()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiButtonView = Manager::Instance::get()->tryGet<UiButtonComponent>(hoveredElement);
	if (!uiButtonView || !uiButtonView->enabled)
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto mouseState = inputSystem->getMouseState(MouseButton::Left);
	auto currState = pressedButton == hoveredElement && mouseState ? "active" : "hovered";
	setUiButtonAnimation(hoveredElement, uiButtonView->animationPath, currState, "default");

	if (!uiButtonView->noCursorHand)
		inputSystem->setCursorType(CursorType::Default);
	if (!mouseState)
		pressedButton = {};
}
void UiButtonSystem::uiButtonStay()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiButtonView = Manager::Instance::get()->tryGet<UiButtonComponent>(hoveredElement);
	if (!uiButtonView || !uiButtonView->enabled)
		return;

	auto inputSystem = InputSystem::Instance::get();
	if (inputSystem->isMousePressed(MouseButton::Left))
	{
		setUiButtonAnimation(hoveredElement, uiButtonView->animationPath, "hovered", "active");
		pressedButton = hoveredElement;
	}
	else if (inputSystem->isMouseReleased(MouseButton::Left))
	{
		if (pressedButton == hoveredElement)
		{
			setUiButtonAnimation(hoveredElement, uiButtonView->animationPath, "active", "hovered");
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
	if (!componentView->enabled)
		serializer.write("isEnabled", false);
	if (componentView->noCursorHand)
		serializer.write("noCursorHand", true);
	if (!componentView->onClick.empty())
		serializer.write("onClick", componentView->onClick);
	if (!componentView->animationPath.empty())
		serializer.write("animationPath", componentView->animationPath);
}
void UiButtonSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiButtonComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("noCursorHand", componentView->noCursorHand);
	deserializer.read("onClick", componentView->onClick);
	deserializer.read("animationPath", componentView->animationPath);
}

//**********************************************************************************************************************
void UiButtonSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiButtonFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
	if (frameView->animateNoCursorHand)
		serializer.write("noCursorHand", (bool)frameView->noCursorHand);
	if (frameView->animateOnClick)
		serializer.write("onClick", frameView->onClick);
	if (frameView->animateAnimationPath)
		serializer.write("animationPath", frameView->animationPath);
}
void UiButtonSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiButtonFrame>(frame);
	
	auto boolValue = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frameView->isEnabled = boolValue;

	boolValue = false;
	frameView->animateNoCursorHand = deserializer.read("noCursorHand", boolValue);
	frameView->noCursorHand = boolValue;

	frameView->animateOnClick = deserializer.read("onClick", frameView->onClick);
	frameView->animateAnimationPath = deserializer.read("animationPath", frameView->animationPath);
}

void UiButtonSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiButtonComponent>(component);
	const auto frameA = View<UiButtonFrame>(a);
	const auto frameB = View<UiButtonFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->enabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateNoCursorHand)
		componentView->noCursorHand = (bool)round(t) ? frameB->noCursorHand : frameA->noCursorHand;
	if (frameA->animateOnClick)
		componentView->onClick = (bool)round(t) ? frameB->onClick : frameA->onClick;
	if (frameA->animateAnimationPath)
		componentView->animationPath = (bool)round(t) ? frameB->animationPath : frameA->animationPath;
}
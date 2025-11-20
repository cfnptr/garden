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
#include "garden/system/ui/transform.hpp"
#include "garden/system/ui/trigger.hpp"
#include "garden/system/ui/label.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/input.hpp"

// TODO: implement input text selector.

using namespace garden;

static void setUiInputAnimation(ID<Entity> element, string_view animationPath, u32string_view text, string_view state)
{
	if (animationPath.empty())
		return;

	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(element);
	if (!transformView)
		return;

	auto animationView = manager->tryGet<AnimationComponent>(element);
	if (animationView)
	{
		animationView->active = animationPath;
		animationView->active.push_back('/');
		animationView->active += state;

		animationView->frame = 0.0f;
		animationView->isPlaying = true;
	}

	auto label = transformView->tryGetChild(0);
	if (label)
	{
		animationView = manager->tryGet<AnimationComponent>(label);
		if (animationView)
		{
			animationView->active = animationPath;
			animationView->active.push_back('/');
			animationView->active += text.empty() ? "text" : "placeholder";
			animationView->frame = 0.0f;
			animationView->isPlaying = true;
		}
	}
}

void UiInputComponent::setEnabled(bool state)
{
	if (enabled == state)
		return;

	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	auto currState = hoveredElement == entity ? "hovered" : "default";
	setUiInputAnimation(entity, animationPath, text, 
		state ? (textBad ? "bad" : currState) : "disabled");

	if (hoveredElement == entity)
	{
		InputSystem::Instance::get()->setCursorType(
			state ? CursorType::Ibeam : CursorType::Default);
	}
	enabled = state;
}
void UiInputComponent::setTextBad(bool state)
{
	if (textBad == state)
		return;

	auto uiInputSystem = UiInputSystem::Instance::get();
	setUiInputAnimation(entity, animationPath, text, enabled ? (state ? "bad" : 
		(entity == uiInputSystem->getActiveInput() ? "active" : "default")) : "disabled");
	textBad = state;
}

//**********************************************************************************************************************
bool UiInputComponent::updateText(bool shrink)
{
	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView)
		return false;
	auto label = transformView->tryGetChild(0);
	if (!label)
		return false;
	auto uiLabelView = manager->tryGet<UiLabelComponent>(label);
	if (!uiLabelView)
		return false;

	uiLabelView->text = prefix;
	uiLabelView->text += text.empty() ? placeholder : 
		(replaceChar == 0 ? text : u32string(text.length(), (char32_t)replaceChar));
	uiLabelView->text.push_back(U' ');
	uiLabelView->color = text.empty() ? placeholderColor : textColor;
	return uiLabelView->updateText(shrink);
}

//**********************************************************************************************************************
bool UiInputComponent::updateCaret(psize charIndex)
{
	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView)
		return false;

	auto label = transformView->tryGetChild(0);
	auto caret = transformView->tryGetChild(1);
	if (!label || !caret)
		return false;

	auto uiLabelView = manager->tryGet<UiLabelComponent>(label);
	if (!uiLabelView || !uiLabelView->getTextData())
		return false;

	auto textSystem = TextSystem::Instance::get();
	auto uiTransformSystem = UiTransformSystem::Instance::get();
	auto textView = textSystem->get(uiLabelView->getTextData());
	auto fontAtlasView = textSystem->get(textView->getFontAtlas());
	auto fontSize = fontAtlasView->getFontSize();
	auto inputScale = (float2)transformView->getScale();
	
	if (text.empty())
	{
		charIndex = 0;
	}
	else
	{
		if (charIndex == SIZE_MAX)
		{
			auto cursorPos = uiTransformSystem->getCursorPosition();
			cursorPos = (float2)(inverse4x4(transformView->calcModel()) * 
				f32x4(cursorPos.x, cursorPos.y, 0.0f, 1.0f));
			cursorPos.x += 0.5f; cursorPos *= inputScale / fontSize;
			charIndex = textView->calcCaretIndex(uiLabelView->text, cursorPos) - prefix.length();
		}
		else charIndex = min(charIndex, text.length());
	}
	caretIndex = charIndex;

	charIndex = min(charIndex + prefix.length(), uiLabelView->text.length() - 1);
	auto caretAdvance = textView->calcCaretAdvance(uiLabelView->text, charIndex) * fontSize;
	caretAdvance.x += 1.0f; caretAdvance.x *= uiTransformSystem->uiScale;
	
	if (caretAdvance.x > inputScale.x)
	{
		// TODO: move text transform.
	}
	
	caretAdvance /= inputScale; caretAdvance.x -= 0.5f;
	inputScale /= uiTransformSystem->uiScale;

	auto caretTransformView = manager->get<TransformComponent>(caret);
	caretTransformView->setPosition(float3(caretAdvance, -0.1f));
	caretTransformView->setScale(float3(1.0f / inputScale.x, (fontSize * 1.25f) / inputScale.y, 1.0f));
	caretTransformView->setActive(true);

	auto animationView = manager->tryGet<AnimationComponent>(caret);
	if (animationView)
		animationView->frame = 0.0f;
	return true;
}
bool UiInputComponent::hideCaret()
{
	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView)
		return false;
	auto caret = transformView->tryGetChild(1);
	if (!caret)
		return false;

	auto caretTransformView = manager->get<TransformComponent>(caret);
	caretTransformView->setActive(false);
	return true;
}

//**********************************************************************************************************************
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

		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputEnter", UiInputSystem::uiInputEnter);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputExit", UiInputSystem::uiInputExit);
		ECSM_UNSUBSCRIBE_FROM_EVENT("UiInputStay", UiInputSystem::uiInputStay);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", UiInputSystem::update);

		manager->unregisterEvent("UiInputEnter");
		manager->unregisterEvent("UiInputExit");
		manager->unregisterEvent("UiInputStay");
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiInputSystem::uiInputEnter()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled)
		return;

	if (hoveredElement != activeInput)
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, uiInputView->text, "hovered");
	InputSystem::Instance::get()->setCursorType(CursorType::Ibeam);
}
void UiInputSystem::uiInputExit()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled)
		return;

	if (hoveredElement != activeInput)
	{
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, 
			uiInputView->text, uiInputView->textBad ? "bad" : "default");
	}
	InputSystem::Instance::get()->setCursorType(CursorType::Default);
}
void UiInputSystem::uiInputStay()
{
	auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();
	if (!hoveredElement)
		return;
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(hoveredElement);
	if (!uiInputView || !uiInputView->enabled || hoveredElement == activeInput)
		return;

	if (InputSystem::Instance::get()->isMousePressed(MouseButton::Left))
	{
		setUiInputAnimation(hoveredElement, uiInputView->animationPath, 
			uiInputView->text, uiInputView->textBad ? "bad" : "active");
		uiInputView->updateCaret();
		activeInput = hoveredElement;
	}
}

//**********************************************************************************************************************
void UiInputSystem::updateActive()
{
	auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(activeInput);
	if (!uiInputView)
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto& keyboardChars = inputSystem->getKeyboardChars32();
	if (!keyboardChars.empty())
	{
		auto freeCharCount = uiInputView->maxLength > uiInputView->text.length() ?
			uiInputView->maxLength - uiInputView->text.length() : 0;
		if (freeCharCount > 0)
		{
			u32string_view appendChars(keyboardChars.data(), min(keyboardChars.length(), freeCharCount));
			if (uiInputView->caretIndex < uiInputView->text.length())
				uiInputView->text.insert(uiInputView->caretIndex, appendChars);
			else uiInputView->text += appendChars;

			uiInputView->updateText();
			uiInputView->updateCaret(uiInputView->caretIndex + keyboardChars.size());

			if (!uiInputView->onChange.empty())
				Manager::Instance::get()->tryRunEvent(uiInputView->onChange);
		}
	}

	if (uiInputView->text.empty())
		return;

	auto isRepeated = false;
	if (inputSystem->getKeyboardState(KeyboardButton::Left) ||
		inputSystem->getKeyboardState(KeyboardButton::Right) ||
		inputSystem->getKeyboardState(KeyboardButton::Backspace) ||
		inputSystem->getKeyboardState(KeyboardButton::Delete))
	{
		if (repeatTime < inputSystem->getSystemTime())
		{
			repeatTime = inputSystem->getSystemTime() + repeatSpeed;
			isRepeated = true;
		}
	}
	else repeatTime = inputSystem->getSystemTime() + repeatDelay;

	if (inputSystem->isMouseReleased(MouseButton::Left))
	{
		uiInputView->updateCaret();
	}
	else if (inputSystem->isKeyboardPressed(KeyboardButton::Right) ||
		(isRepeated && inputSystem->getKeyboardState(KeyboardButton::Right)))
	{
		uiInputView->updateCaret(uiInputView->caretIndex + 1);
	}
	else if (inputSystem->isKeyboardPressed(KeyboardButton::Left) ||
		(isRepeated && inputSystem->getKeyboardState(KeyboardButton::Left)))
	{
		if (uiInputView->caretIndex > 0)
			uiInputView->updateCaret(uiInputView->caretIndex - 1);
	}

	if (inputSystem->isKeyboardPressed(KeyboardButton::Backspace) || 
		(isRepeated && inputSystem->getKeyboardState(KeyboardButton::Backspace)))
	{
		if (uiInputView->caretIndex > 0 && uiInputView->caretIndex <= uiInputView->text.length())
		{
			uiInputView->text.erase(uiInputView->caretIndex - 1, 1);
			uiInputView->updateText();
			uiInputView->updateCaret(uiInputView->caretIndex - 1);

			if (!uiInputView->onChange.empty())
				Manager::Instance::get()->tryRunEvent(uiInputView->onChange);
		}
	}
	if (inputSystem->isKeyboardPressed(KeyboardButton::Delete) ||
		(isRepeated && inputSystem->getKeyboardState(KeyboardButton::Delete)))
	{
		if (uiInputView->caretIndex < uiInputView->text.length())
		{
			uiInputView->text.erase(uiInputView->caretIndex, 1);
			uiInputView->updateText();

			if (!uiInputView->onChange.empty())
				Manager::Instance::get()->tryRunEvent(uiInputView->onChange);
		}
	}
}
void UiInputSystem::update()
{
	if (activeInput)
	{
		auto inputSystem = InputSystem::Instance::get();
		auto hoveredElement = UiTriggerSystem::Instance::get()->getHovered();

		if (inputSystem->getCursorMode() != CursorMode::Normal || 
			(inputSystem->isMousePressed(MouseButton::Left) && activeInput != hoveredElement))
		{
			auto uiInputView = Manager::Instance::get()->tryGet<UiInputComponent>(activeInput);
			if (uiInputView)
			{
				setUiInputAnimation(activeInput, uiInputView->animationPath, 
					uiInputView->text, uiInputView->textBad ? "bad" : "default");
				uiInputView->hideCaret();
			}
			activeInput = {};
		}
		else 
		{
			updateActive();
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
	if (!componentView->enabled)
		serializer.write("isEnabled", false);
	if (componentView->textBad)
		serializer.write("isTextBad", true);
	if (!componentView->text.empty())
		serializer.write("text", componentView->text);
	if (!componentView->placeholder.empty())
		serializer.write("placeholder", componentView->placeholder);
	if (!componentView->prefix.empty())
		serializer.write("prefix", componentView->prefix);
	if (!componentView->onChange.empty())
		serializer.write("onChange", componentView->onChange);
	if (!componentView->animationPath.empty())
		serializer.write("animationPath", componentView->animationPath);
	if (componentView->textColor != f32x4::one)
		serializer.write("textColor", (float4)componentView->textColor);
	if (componentView->placeholderColor != f32x4(0.5f, 0.5f, 0.5f, 1.0f))
		serializer.write("placeholderColor", (float4)componentView->placeholderColor);
	if (componentView->maxLength != UINT32_MAX)
		serializer.write("maxLength", componentView->maxLength);
	if (componentView->replaceChar != 0)
		serializer.write("replaceChar", componentView->replaceChar);
}
void UiInputSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiInputComponent>(component);
	deserializer.read("isEnabled", componentView->enabled);
	deserializer.read("isTextBad", componentView->textBad);
	deserializer.read("text", componentView->text);
	deserializer.read("placeholder", componentView->placeholder);
	deserializer.read("prefix", componentView->prefix);
	deserializer.read("onChange", componentView->onChange);
	deserializer.read("animationPath", componentView->animationPath);
	deserializer.read("textColor", componentView->textColor);
	deserializer.read("placeholderColor", componentView->placeholderColor);
	deserializer.read("maxLength", componentView->maxLength);
	deserializer.read("replaceChar", componentView->replaceChar);
}

//**********************************************************************************************************************
void UiInputSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiInputFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
}
void UiInputSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiInputFrame>(frame);
	frameView->animateIsEnabled = deserializer.read("isEnabled", frameView->isEnabled);
}

void UiInputSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiInputComponent>(component);
	const auto frameA = View<UiInputFrame>(a);
	const auto frameB = View<UiInputFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->enabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
}
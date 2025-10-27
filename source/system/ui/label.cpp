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
#include "garden/system/resource.hpp"

using namespace garden;

bool UiLabelComponent::updateText(bool shrink)
{
	auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, loadNoto);
	if (fonts.empty())
		return false;

	auto textSystem = TextSystem::Instance::get();
	if (text)
	{
		auto textView = textSystem->get(text);
		if (textView->isDynamic())
			return textView->update(value, std::move(fonts), propterties, shrink);
		textSystem->destroy(text);
	}

	text = textSystem->createText(value, std::move(fonts), fontSize, propterties);
	return (bool)text;
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
	if (!uiLabelView->fontPaths.empty())
	{
		const auto& fontPaths = uiLabelView->fontPaths;
		serializer.beginChild("fontPaths");
		for (const auto& fontPath : fontPaths)
		{
			serializer.beginArrayElement();
			serializer.write(fontPath.generic_string());
			serializer.endArrayElement();
		}
		serializer.endChild();
	}
	if (uiLabelView->loadNoto != true)
		serializer.write("loadNoto", uiLabelView->loadNoto);
	#endif
}
void UiLabelSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto uiLabelView = View<UiLabelComponent>(component);
	deserializer.read("value", uiLabelView->value);

	vector<fs::path> fontPaths;
	if (deserializer.beginChild("fontPaths"))
	{
		auto arraySize = (uint32)deserializer.getArraySize();
		for (uint32 i = 0; i < arraySize; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			string fontPath;
			deserializer.read(fontPath);
			if (fontPath.empty())
				fontPath = "missing";
			fontPaths.push_back(std::move(fontPath));

			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	auto loadNoto = true;
	deserializer.read("loadNoto", loadNoto);

	if (!fontPaths.empty() || loadNoto)
		abort(); // TODO: load fonts and text

	#if GARDEN_DEBUG || GARDEN_EDITOR
	uiLabelView->fontPaths = std::move(fontPaths);
	uiLabelView->loadNoto = loadNoto;
	#endif
}

//**********************************************************************************************************************
void UiLabelSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto uiLabelFrameView = View<UiLabelFrame>(frame);
	if (uiLabelFrameView->animateValue)
	{
		serializer.write("value", uiLabelFrameView->value);

		#if GARDEN_DEBUG || GARDEN_EDITOR
		if (!uiLabelFrameView->fontPaths.empty())
		{
			const auto& fontPaths = uiLabelFrameView->fontPaths;
			serializer.beginChild("fontPaths");
			for (const auto& fontPath : fontPaths)
			{
				serializer.beginArrayElement();
				serializer.write(fontPath.generic_string());
				serializer.endArrayElement();
			}
			serializer.endChild();
		}
		if (uiLabelFrameView->loadNoto != true)
			serializer.write("loadNoto", uiLabelFrameView->loadNoto);
		#endif
	}
}
ID<AnimationFrame> UiLabelSystem::deserializeAnimation(IDeserializer& deserializer)
{
	UiLabelFrame frame;
	frame.animateValue = deserializer.read("value", frame.value);

	vector<fs::path> fontPaths;
	if (deserializer.beginChild("fontPaths"))
	{
		auto arraySize = (uint32)deserializer.getArraySize();
		for (uint32 i = 0; i < arraySize; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			string fontPath;
			deserializer.read(fontPath);
			if (fontPath.empty())
				fontPath = "missing";
			fontPaths.push_back(std::move(fontPath));

			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	auto loadNoto = true;
	deserializer.read("loadNoto", loadNoto);

	if (!fontPaths.empty() || loadNoto)
		abort(); // TODO: load fonts and text
	
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.fontPaths = std::move(fontPaths);
	frame.loadNoto = loadNoto;
	#endif

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
				uiLabelView->fontPaths = frameB->fontPaths;
				uiLabelView->loadNoto = frameB->loadNoto;
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
				uiLabelView->fontPaths = frameA->fontPaths;
				uiLabelView->loadNoto = frameA->loadNoto;
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
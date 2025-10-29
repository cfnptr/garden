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
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

bool UiLabelComponent::updateText(bool shrink)
{
	auto textSystem = TextSystem::Instance::get();
	#if GARDEN_DEBUG || GARDEN_EDITOR
	auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, loadNoto);
	if (fonts.empty())
		return false;

	if (text)
	{
		auto textView = textSystem->get(text);
		return textView->update(value, propterties, std::move(fonts), fontSize, 
			FontAtlas::defaultImageFlags, Text::defaultBufferFlags, shrink);
	}

	text = textSystem->createText(value, std::move(fonts), fontSize, propterties);
	return (bool)text;
	#else
	if (!text)
		return false;

	auto textView = textSystem->get(text);
	auto fonts = textSystem->get(textView->getFontAtlas())->getFonts();
	return textView->update(value, propterties, std::move(fonts), fontSize, 
		FontAtlas::defaultImageFlags, Text::defaultBufferFlags, shrink);
	#endif
}

UiLabelSystem::UiLabelSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
	manager->addGroupSystem<IMeshRenderSystem>(this);
}
UiLabelSystem::~UiLabelSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
		manager->removeGroupSystem<IMeshRenderSystem>(this);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
void UiLabelSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<UiLabelComponent>(component);
	TextSystem::Instance::get()->destroy(componentView->text);

	if (full)
		**componentView = {};
	else
		componentView->text = {};
}
void UiLabelSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiLabelComponent>(source);
	auto destinationView = View<UiLabelComponent>(destination);
	**destinationView = **sourceView;

	if (sourceView->text)
	{
		auto textSystem = TextSystem::Instance::get();
		auto srcTextView = textSystem->get(sourceView->text);
		if (srcTextView->isAtlasShared())
		{
			destinationView->text = textSystem->createText(srcTextView->getValue(), 
				srcTextView->getFontAtlas(), srcTextView->getProperties(), true);
		}
		else
		{
			auto fonts = textSystem->get(srcTextView->getFontAtlas())->getFonts();
			destinationView->text = textSystem->createText(srcTextView->getValue(), 
				std::move(fonts), sourceView->fontSize, srcTextView->getProperties());
		}
	}
}
string_view UiLabelSystem::getComponentName() const
{
	return "Label UI";
}

//**********************************************************************************************************************
MeshRenderType UiLabelSystem::getMeshRenderType() const
{
	return MeshRenderType::UI;
}
void UiLabelSystem::drawAsync(MeshRenderComponent* meshRenderView, 
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto uiLabelView = (UiLabelComponent*)meshRenderView;
	if (!uiLabelView->text)
		return;

	// TODO:
}
ID<GraphicsPipeline> UiLabelSystem::createBasePipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();

	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = true;
	options.loadAsync = false;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"text/ui", deferredSystem->getUiFramebuffer(), options);
}
uint64 UiLabelSystem::getBaseInstanceDataSize()
{
	return sizeof(Text::Instance);
}

//**********************************************************************************************************************
void UiLabelSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiLabelComponent>(component);
	if (!componentView->value.empty())
		serializer.write("value", componentView->value);
	if (componentView->propterties.color != Color::white)
		serializer.write("color", componentView->propterties.color);
	if (componentView->propterties.alignment != Text::Alignment::Center)
		serializer.write("alignment", toString(componentView->propterties.alignment));
	if (componentView->propterties.isBold != false)
		serializer.write("isBold", componentView->propterties.isBold);
	if (componentView->propterties.isItalic != false)
		serializer.write("isItalic", componentView->propterties.isItalic);
	if (componentView->propterties.useTags != false)
		serializer.write("useTags", componentView->propterties.useTags);
	if (componentView->fontSize != 16)
		serializer.write("fontSize", componentView->fontSize);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!componentView->fontPaths.empty())
	{
		const auto& fontPaths = componentView->fontPaths;
		serializer.beginChild("fontPaths");
		for (const auto& fontPath : fontPaths)
		{
			serializer.beginArrayElement();
			serializer.write(fontPath.generic_string());
			serializer.endArrayElement();
		}
		serializer.endChild();
	}
	if (componentView->loadNoto != true)
		serializer.write("loadNoto", componentView->loadNoto);
	#endif
}
void UiLabelSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiLabelComponent>(component);
	deserializer.read("value", componentView->value);
	deserializer.read("color", componentView->propterties.color);
	deserializer.read("isBold", componentView->propterties.isBold);
	deserializer.read("isItalic", componentView->propterties.isItalic);
	deserializer.read("useTags", componentView->propterties.useTags);
	deserializer.read("fontSize", componentView->fontSize);

	string alignment;
	if (deserializer.read("alignment", alignment))
		toTextAlignment(alignment, componentView->propterties.alignment);

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

	componentView->fontSize = max(componentView->fontSize, 1u);
	if (!fontPaths.empty() || loadNoto)
	{
		auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, 0, loadNoto);
		componentView->text = TextSystem::Instance::get()->createText(componentView->value, 
			std::move(fonts), componentView->fontSize, componentView->propterties);
	}

	#if GARDEN_DEBUG || GARDEN_EDITOR
	componentView->fontPaths = std::move(fontPaths);
	componentView->loadNoto = loadNoto;
	#endif
}

//**********************************************************************************************************************
void UiLabelSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiLabelFrame>(frame);
	if (!frameView->value.empty())
		serializer.write("value", frameView->value);
	if (frameView->propterties.color != Color::white)
		serializer.write("color", frameView->propterties.color);
	if (frameView->propterties.alignment != Text::Alignment::Center)
		serializer.write("alignment", toString(frameView->propterties.alignment));
	if (frameView->propterties.isBold != false)
		serializer.write("isBold", frameView->propterties.isBold);
	if (frameView->propterties.isItalic != false)
		serializer.write("isItalic", frameView->propterties.isItalic);
	if (frameView->propterties.useTags != false)
		serializer.write("useTags", frameView->propterties.useTags);
	if (frameView->fontSize != 16)
		serializer.write("fontSize", frameView->fontSize);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!frameView->fontPaths.empty())
	{
		const auto& fontPaths = frameView->fontPaths;
		serializer.beginChild("fontPaths");
		for (const auto& fontPath : fontPaths)
		{
			serializer.beginArrayElement();
			serializer.write(fontPath.generic_string());
			serializer.endArrayElement();
		}
		serializer.endChild();
	}
	if (frameView->loadNoto != true)
		serializer.write("loadNoto", frameView->loadNoto);
	#endif
}
void UiLabelSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiLabelFrame>(frame);
	deserializer.read("value", frameView->value);
	deserializer.read("color", frameView->propterties.color);
	deserializer.read("isBold", frameView->propterties.isBold);
	deserializer.read("isItalic", frameView->propterties.isItalic);
	deserializer.read("useTags", frameView->propterties.useTags);
	deserializer.read("fontSize", frameView->fontSize);

	string alignment;
	if (deserializer.read("alignment", alignment))
		toTextAlignment(alignment, frameView->propterties.alignment);

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
	{
		auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, 0, loadNoto);
		frameView->text = TextSystem::Instance::get()->createText(frameView->value, 
			std::move(fonts), frameView->fontSize, frameView->propterties);
	}
	
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frameView->fontPaths = std::move(fontPaths);
	frameView->loadNoto = loadNoto;
	#endif
}

void UiLabelSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiLabelComponent>(component);
	const auto frameA = View<UiLabelFrame>(a);
	const auto frameB = View<UiLabelFrame>(b);

	if ((bool)round(t))
	{
		if (frameB->text)
		{
			componentView->text = frameB->text;
			componentView->value = frameB->value;
			componentView->propterties = frameB->propterties;
			componentView->fontSize = frameB->fontSize;
			#if GARDEN_DEBUG || GARDEN_EDITOR
			componentView->fontPaths = frameB->fontPaths;
			componentView->loadNoto = frameB->loadNoto;
			#endif
		}
	}
	else
	{
		if (frameA->text)
		{
			componentView->text = frameA->text;
			componentView->value = frameA->value;
			componentView->propterties = frameA->propterties;
			componentView->fontSize = frameA->fontSize;
			#if GARDEN_DEBUG || GARDEN_EDITOR
			componentView->fontPaths = frameA->fontPaths;
			componentView->loadNoto = frameA->loadNoto;
			#endif
		}
	}
}

void UiLabelSystem::resetAnimation(View<AnimationFrame> frame, bool full)
{
	auto frameView = View<UiLabelFrame>(frame);
	TextSystem::Instance::get()->destroy(frameView->text);

	if (full)
		**frameView = {};
	else
		frameView->text = {};
}
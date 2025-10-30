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
#include "math/matrix/transform.hpp"

using namespace garden;

static ID<GraphicsPipeline> createPipeline()
{
	auto deferredSystem = DeferredRenderSystem::Instance::get();

	ResourceSystem::GraphicsOptions options;
	options.useAsyncRecording = true;
	options.loadAsync = false; // Note: we need text immediately.

	return ResourceSystem::Instance::get()->loadGraphicsPipeline(
		"text/ui", deferredSystem->getUiFramebuffer(), options);
}
static DescriptorSet::Uniforms getUniforms(ID<Text> text)
{
	auto textSystem = TextSystem::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto textView = TextSystem::Instance::get()->get(text);
	auto fontAtlasView = textSystem->get(textView->getFontAtlas());
	auto fontAtlas = graphicsSystem->get(fontAtlasView->getImage());

	DescriptorSet::Uniforms uniforms =
	{ 
		{ "fontAtlas", DescriptorSet::Uniform(fontAtlas->getDefaultView()) },
		{ "instance", DescriptorSet::Uniform(textView->getInstanceBuffer()) }
	};
	return uniforms;
}

//**********************************************************************************************************************
bool UiLabelComponent::updateText(bool shrink)
{
	auto textSystem = TextSystem::Instance::get();
	auto graphicsSystem = GraphicsSystem::Instance::get();

	if (value.empty())
	{
		if (shrink)
		{
			graphicsSystem->destroy(descriptorSet);
			textSystem->destroy(text);
			text = {}; descriptorSet = {}; 
		}

		isEnabled = false;
		return true;
	}

	ID<Image> currFontAtlas = {}; ID<Buffer> currInstanceBuffer = {};
	if (text)
	{
		auto textView = textSystem->get(text);
		auto fontAtlasView = textSystem->get(textView->getFontAtlas());
		auto fonts = fontAtlasView->getFonts();
		currFontAtlas = fontAtlasView->getImage();
		currInstanceBuffer = textView->getInstanceBuffer();

		auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::Frame);

		if (!textView->update(value, fontSize, propterties, std::move(fonts), 
			FontAtlas::defaultImageFlags, Text::defaultBufferFlags, shrink))
		{
			return false;
		}

		if (stopRecording)
			graphicsSystem->stopRecording();
	}
	else
	{
		#if GARDEN_DEBUG || GARDEN_EDITOR
		auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, 0, loadNoto);
		if (fonts.empty())
			return false;

		text = textSystem->createText(value, std::move(fonts), fontSize, propterties);
		if (!text)
			return false;
		#else
		return false;
		#endif
	}

	auto textView = textSystem->get(text);
	if (!descriptorSet || currInstanceBuffer != textView->getInstanceBuffer() ||
		currFontAtlas != textSystem->get(textView->getFontAtlas())->getImage())
	{
		graphicsSystem->destroy(descriptorSet);

		auto uniforms = getUniforms(text);
		descriptorSet = graphicsSystem->createDescriptorSet(
			UiLabelSystem::Instance::get()->getPipeline(), std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.uiLabel" + to_string(*descriptorSet));
	}

	aabb.setSize(f32x4(float3(textView->getSize() * fontSize, 1.0f)));
	isEnabled = true;
	return true;
}

//**********************************************************************************************************************
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
bool UiLabelSystem::isDrawReady(int8 shadowPass)
{
	if (shadowPass >= 0)
		return false; // Note: no shadow pass for UI.
	if (!pipeline)
		pipeline = createPipeline();
	return GraphicsSystem::Instance::get()->get(pipeline)->isReady();
}
void UiLabelSystem::prepareDraw(const f32x4x4& viewProj, uint32 drawCount, int8 shadowPass)
{
	graphicsSystem = GraphicsSystem::Instance::get();
	textSystem = TextSystem::Instance::get();
	pipelineView = graphicsSystem->get(pipeline);
}
void UiLabelSystem::beginDrawAsync(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(float4::zero, taskIndex); // TODO: calc and set UI scissor.
}
void UiLabelSystem::drawAsync(MeshRenderComponent* meshRenderView, 
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto uiLabelView = (UiLabelComponent*)meshRenderView;
	if (!uiLabelView->text)
		return;

	auto textView = textSystem->get(uiLabelView->text);
	if (!graphicsSystem->get(textView->getInstanceBuffer())->isReady())
		return;
	auto fontAtlasView = textSystem->get(textView->getFontAtlas());
	if (!graphicsSystem->get(fontAtlasView->getImage())->isReady())
		return;

	PushConstants pc;
	auto fontSize = fontAtlasView->getFontSize();
	pc.mvp = (float4x4)(viewProj * model * scale(f32x4(fontSize, fontSize, 1.0f)));

	pipelineView->bindDescriptorSetAsync(uiLabelView->descriptorSet, 0, taskIndex);
	pipelineView->pushConstantsAsync(&pc, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6, textView->getInstanceCount());
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
		auto textSystem = TextSystem::Instance::get();
		auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, 0, loadNoto);
		componentView->text = textSystem->createText(componentView->value, 
			std::move(fonts), componentView->fontSize, componentView->propterties);
		
		auto uniforms = getUniforms(componentView->text);
		auto descriptorSet = graphicsSystem->createDescriptorSet(
			UiLabelSystem::Instance::get()->getPipeline(), std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.uiLabel" + to_string(*descriptorSet));
		componentView->descriptorSet = descriptorSet;

		auto textView = textSystem->get(componentView->text);
		componentView->aabb.setSize(f32x4(float3(textView->getSize() * componentView->fontSize, 1.0f)));
		componentView->isEnabled = true;
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

		auto uniforms = getUniforms(frameView->text);
		auto descriptorSet = graphicsSystem->createDescriptorSet(
			UiLabelSystem::Instance::get()->getPipeline(), std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.uiLabel" + to_string(*descriptorSet));
		frameView->descriptorSet = descriptorSet;
	}
	
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frameView->fontPaths = std::move(fontPaths);
	frameView->loadNoto = loadNoto;
	#endif
}

//**********************************************************************************************************************
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
			componentView->descriptorSet = frameB->descriptorSet;
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
			componentView->descriptorSet = frameA->descriptorSet;
			componentView->value = frameA->value;
			componentView->propterties = frameA->propterties;
			componentView->fontSize = frameA->fontSize;
			#if GARDEN_DEBUG || GARDEN_EDITOR
			componentView->fontPaths = frameA->fontPaths;
			componentView->loadNoto = frameA->loadNoto;
			#endif
		}
	}

	if (componentView->text)
	{
		auto textView = textSystem->get(componentView->text);
		componentView->aabb.setSize(f32x4(float3(textView->getSize() * componentView->fontSize, 1.0f)));
		componentView->isEnabled = true;
	}
	else
	{
		componentView->isEnabled = false;
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

//**********************************************************************************************************************
ID<GraphicsPipeline> UiLabelSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}
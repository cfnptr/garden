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
#include "garden/system/ui/transform.hpp"
#include "garden/system/ui/scissor.hpp"
#include "garden/system/transform.hpp"
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
	auto uiScale = UiTransformSystem::Instance::get()->uiScale;
	// TODO: take into account macOS differend window and framebuffer scale!

	if (text.empty() || uiScale <= 0.0f)
	{
		if (shrink)
		{
			graphicsSystem->destroy(descriptorSet);
			textSystem->destroy(textData);
			textData = {}; descriptorSet = {}; 
		}

		isEnabled = false;
		return true;
	}

	auto scaledFontSize = (uint32)ceil(fontSize / uiScale);
	ID<Image> currFontAtlas = {}; ID<Buffer> currInstanceBuffer = {};

	if (textData)
	{
		auto textView = textSystem->get(textData);
		auto fontAtlasView = textSystem->get(textView->getFontAtlas());
		auto fonts = fontAtlasView->getFonts();
		currFontAtlas = fontAtlasView->getImage();
		currInstanceBuffer = textView->getInstanceBuffer();

		auto stopRecording = graphicsSystem->tryStartRecording(CommandBufferType::Frame);

		if (!textView->update(text, scaledFontSize, propterties, std::move(fonts), 
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

		textData = textSystem->createText(text, std::move(fonts), scaledFontSize, propterties);
		if (!textData)
			return false;
		#else
		return false;
		#endif
	}

	auto textView = textSystem->get(textData);
	if (!descriptorSet || currInstanceBuffer != textView->getInstanceBuffer() ||
		currFontAtlas != textSystem->get(textView->getFontAtlas())->getImage())
	{
		graphicsSystem->destroy(descriptorSet);

		auto uniforms = getUniforms(textData);
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
	ECSM_SUBSCRIBE_TO_EVENT("Update", UiLabelSystem::update);
}
UiLabelSystem::~UiLabelSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
		manager->removeGroupSystem<IMeshRenderSystem>(this);
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", UiLabelSystem::update);
	}
	unsetSingleton();
}

void UiLabelSystem::update()
{
	auto newUiScale = UiTransformSystem::Instance::get()->uiScale;
	if (lastUiScale != newUiScale)
	{
		for (auto& component : components)
		{
			if (!component.textData || component.text.empty())
				continue;
			component.updateText();
		}
		lastUiScale = newUiScale;
	}
}

void UiLabelSystem::resetComponent(View<Component> component, bool full)
{
	auto componentView = View<UiLabelComponent>(component);
	GraphicsSystem::Instance::get()->destroy(componentView->descriptorSet);
	TextSystem::Instance::get()->destroy(componentView->textData);

	if (full)
	{
		**componentView = {};
	}
	else
	{
		componentView->textData = {}; componentView->descriptorSet = {};
	}
}
void UiLabelSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiLabelComponent>(source);
	auto destinationView = View<UiLabelComponent>(destination);
	**destinationView = **sourceView;

	if (sourceView->textData)
	{
		auto textSystem = TextSystem::Instance::get();
		auto srcTextView = textSystem->get(sourceView->textData);

		ID<Text> text;
		if (srcTextView->isAtlasShared())
		{
			text = textSystem->createText(sourceView->text, 
				srcTextView->getFontAtlas(), srcTextView->getProperties(), true);
		}
		else
		{
			auto fonts = textSystem->get(srcTextView->getFontAtlas())->getFonts();
			text = textSystem->createText(sourceView->text, std::move(fonts), 
				sourceView->fontSize, srcTextView->getProperties());
		}
		destinationView->textData = text;

		auto uniforms = getUniforms(text);
		auto descriptorSet = GraphicsSystem::Instance::get()->createDescriptorSet(
			UiLabelSystem::Instance::get()->getPipeline(), std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.uiLabel" + to_string(*descriptorSet));
		destinationView->descriptorSet = descriptorSet;
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
	manager = Manager::Instance::get();
	textSystem = TextSystem::Instance::get();
	uiScissorSystem = UiScissorSystem::Instance::tryGet();
	pipelineView = OptView<GraphicsPipeline>(GraphicsSystem::Instance::get()->get(pipeline));
}
void UiLabelSystem::beginDrawAsync(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);

	if (uiScissorSystem)
		pipelineView->setViewportAsync(float4::zero, taskIndex);
	else pipelineView->setViewportScissorAsync(float4::zero, taskIndex);
}
void UiLabelSystem::drawAsync(MeshRenderComponent* meshRenderView, 
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto uiLabelView = (UiLabelComponent*)meshRenderView;
	if (!uiLabelView->textData)
		return;

	auto textView = textSystem->get(uiLabelView->textData);
	if (!textView->isReady() || textView->getInstanceCount() == 0)
		return;

	auto entity = uiLabelView->getEntity(); auto fontSize = uiLabelView->fontSize;
	auto transformView = manager->get<TransformComponent>(entity);
	auto localScale = transformView->getScale() * f32x4(fontSize, fontSize, 1.0f);
	auto localModel = scale(model, (1.0f / extractScale(model)) * localScale);
	setTranslation(localModel, (f32x4)u32x4(getTranslation(localModel) / lastUiScale) * lastUiScale);
	// TODO: take into account macOS differend window and framebuffer scale!

	PushConstants pc;
	pc.mvp = (float4x4)(viewProj * localModel);
	pc.color = (float4)srgbToRgb(uiLabelView->color);

	if (uiScissorSystem)
		pipelineView->setScissorAsync(uiScissorSystem->calcScissor(entity), taskIndex);
	pipelineView->bindDescriptorSetAsync(uiLabelView->descriptorSet, 0, taskIndex);
	pipelineView->pushConstantsAsync(&pc, taskIndex);
	pipelineView->drawAsync(taskIndex, {}, 6, textView->getInstanceCount());
}

//**********************************************************************************************************************
void UiLabelSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiLabelComponent>(component);
	if (!componentView->text.empty())
		serializer.write("text", componentView->text);
	if (componentView->color != f32x4::one)
		serializer.write("color", (float4)componentView->color);
	if (componentView->propterties.alignment != Text::Alignment::Center)
		serializer.write("alignment", toString(componentView->propterties.alignment));
	if (componentView->propterties.isBold)
		serializer.write("isBold", true);
	if (componentView->propterties.isItalic)
		serializer.write("isItalic", true);
	if (componentView->propterties.useTags)
		serializer.write("useTags", true);
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
	deserializer.read("text", componentView->text);
	deserializer.read("color", componentView->color);
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

	auto uiScale = UiTransformSystem::Instance::get()->uiScale;
	// TODO: take into account macOS differend window and framebuffer scale!
	componentView->fontSize = max((uint32)ceil(componentView->fontSize / uiScale), 1u);

	if (!fontPaths.empty() || loadNoto)
	{
		auto textSystem = TextSystem::Instance::get();
		auto fonts = ResourceSystem::Instance::get()->loadFonts(fontPaths, 0, loadNoto);
		auto text = textSystem->createText(componentView->text, 
			std::move(fonts), componentView->fontSize, componentView->propterties);
		componentView->textData = text;
		
		auto uniforms = getUniforms(text);
		auto descriptorSet = GraphicsSystem::Instance::get()->createDescriptorSet(
			UiLabelSystem::Instance::get()->getPipeline(), std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.uiLabel" + to_string(*descriptorSet));
		componentView->descriptorSet = descriptorSet;

		auto textView = textSystem->get(text);
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
	if (!frameView->text.empty())
		serializer.write("text", frameView->text);
	if (frameView->color != f32x4::one)
		serializer.write("color", (float4)frameView->color);
	if (frameView->propterties.alignment != Text::Alignment::Center)
		serializer.write("alignment", toString(frameView->propterties.alignment));
	if (frameView->propterties.isBold)
		serializer.write("isBold", true);
	if (frameView->propterties.isItalic)
		serializer.write("isItalic", true);
	if (frameView->propterties.useTags)
		serializer.write("useTags", true);
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
	deserializer.read("text", frameView->text);
	deserializer.read("color", frameView->color);
	deserializer.read("isBold", frameView->propterties.isBold);
	deserializer.read("isItalic", frameView->propterties.isItalic);
	deserializer.read("useTags", frameView->propterties.useTags);
	deserializer.read("fontSize", frameView->fontSize);

	string alignment;
	if (deserializer.read("alignment", alignment))
		toTextAlignment(alignment, frameView->propterties.alignment);

	auto uiScale = UiTransformSystem::Instance::get()->uiScale;
	// TODO: take into account macOS differend window and framebuffer scale!
	frameView->fontSize = max((uint32)ceil(frameView->fontSize / uiScale), 1u);

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
		auto text = TextSystem::Instance::get()->createText(frameView->text, 
			std::move(fonts), frameView->fontSize, frameView->propterties);
		frameView->textData = text;

		auto uniforms = getUniforms(text);
		auto descriptorSet = GraphicsSystem::Instance::get()->createDescriptorSet(
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
		if (frameB->textData)
		{
			componentView->text = frameB->text;
			componentView->propterties = frameB->propterties;
			componentView->fontSize = frameB->fontSize;
			componentView->textData = frameB->textData;
			componentView->descriptorSet = frameB->descriptorSet;
			#if GARDEN_DEBUG || GARDEN_EDITOR
			componentView->fontPaths = frameB->fontPaths;
			componentView->loadNoto = frameB->loadNoto;
			#endif
		}
	}
	else
	{
		if (frameA->textData)
		{
			componentView->text = frameA->text;
			componentView->propterties = frameA->propterties;
			componentView->fontSize = frameA->fontSize;
			componentView->textData = frameA->textData;
			componentView->descriptorSet = frameA->descriptorSet;
			#if GARDEN_DEBUG || GARDEN_EDITOR
			componentView->fontPaths = frameA->fontPaths;
			componentView->loadNoto = frameA->loadNoto;
			#endif
		}
	}

	componentView->color = lerp(frameA->color, frameB->color, t);

	if (componentView->textData)
	{
		auto textView = textSystem->get(componentView->textData);
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
	GraphicsSystem::Instance::get()->destroy(frameView->descriptorSet);
	TextSystem::Instance::get()->destroy(frameView->textData);

	if (full)
	{
		**frameView = {};
	}
	else
	{
		frameView->textData = {}; frameView->descriptorSet = {};
	}
}

//**********************************************************************************************************************
ID<GraphicsPipeline> UiLabelSystem::getPipeline()
{
	if (!pipeline)
		pipeline = createPipeline();
	return pipeline;
}
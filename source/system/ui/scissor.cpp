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

#include "garden/system/ui/scissor.hpp"
#include "garden/system/ui/transform.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/input.hpp"
#include "math/matrix/transform.hpp"

using namespace garden;

UiScissorSystem::UiScissorSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
}
UiScissorSystem::~UiScissorSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
	}
	unsetSingleton();
}

string_view UiScissorSystem::getComponentName() const
{
	return "Scissor UI";
}

void UiScissorSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<UiScissorComponent>(component);
	if (componentView->offset != float2::zero)
		serializer.write("offset", componentView->offset);
	if (componentView->scale != float2::one)
		serializer.write("scale", componentView->scale);
	if (componentView->useItsels)
		serializer.write("useItsels", true);
}
void UiScissorSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<UiScissorComponent>(component);
	deserializer.read("offset", componentView->offset);
	deserializer.read("scale", componentView->scale);
	deserializer.read("useItsels", componentView->useItsels);
}

//**********************************************************************************************************************
void UiScissorSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<UiScissorFrame>(frame);
	if (frameView->animateOffset)
		serializer.write("offset", frameView->offset);
	if (frameView->animateScale)
		serializer.write("scale", frameView->scale);
}
void UiScissorSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<UiScissorFrame>(frame);
	frameView->animateOffset = deserializer.read("offset", frameView->offset);
	frameView->animateScale = deserializer.read("scale", frameView->scale);
}

void UiScissorSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<UiScissorComponent>(component);
	const auto frameA = View<UiScissorFrame>(a);
	const auto frameB = View<UiScissorFrame>(b);

	if (frameA->animateOffset)
		componentView->offset = lerp(frameA->offset, frameB->offset, t);
	if (frameA->animateScale)
		componentView->scale = lerp(frameA->scale, frameB->scale, t);
}

//**********************************************************************************************************************
static int4 calcUiScissor(const UiScissorComponent* uiScissorView, const TransformComponent* transformView, 
	float2 windowScale, float2 uiHalfSize, float invUiScale) noexcept
{
	auto thisPos = transformView->getPosition();
	auto model = transformView->calcModel() * scale(translate(
		f32x4(uiScissorView->offset.x - thisPos.getX(), uiScissorView->offset.y - thisPos.getY(), 1.0f)), 
		f32x4(uiScissorView->scale.x, uiScissorView->scale.y, 1.0f));
	auto min = float2(model * f32x4(-0.5f, -0.5f, 0.0f, 1.0f));
	auto max = float2(model * f32x4( 0.5f,  0.5f, 0.0f, 1.0f));

	return int4(int2(fma(min, windowScale, uiHalfSize) * invUiScale),
		int2(fma(max, windowScale, uiHalfSize) * invUiScale));
}
int4 UiScissorSystem::calcScissor(ID<Entity> entity) const noexcept
{
	auto manager = Manager::Instance::get();
	auto transformView = manager->tryGet<TransformComponent>(entity);
	if (!transformView)
		return int4::zero;

	auto inputSystem = InputSystem::Instance::get();
	auto uiTransformSystem = UiTransformSystem::Instance::get();
	auto windowScale = inputSystem->getWindowScale();
	auto framebufferSize = inputSystem->getFramebufferSize();
	auto uiHalfSize = uiTransformSystem->getUiSize() * 0.5f;
	auto uiScale = 1.0f / uiTransformSystem->uiScale;
	auto scissor = int4(int2::zero, framebufferSize);

	auto uiScissorView = manager->tryGet<UiScissorComponent>(entity);
	if (uiScissorView && uiScissorView->useItsels)
	{
		auto newScissor = calcUiScissor(*uiScissorView, *transformView, windowScale, uiHalfSize, uiScale);
		scissor = int4(max((int2)scissor, (int2)newScissor), min(
			int2(scissor.z, scissor.w), int2(newScissor.z, newScissor.w)));
	}

	auto parent = transformView->getParent();
	while (parent)
	{
		auto parentTransformView = manager->get<TransformComponent>(parent);
		uiScissorView = manager->tryGet<UiScissorComponent>(parent);
		if (uiScissorView)
		{
			auto newScissor = calcUiScissor(*uiScissorView, *transformView, windowScale, uiHalfSize, uiScale);
			scissor = int4(max((int2)scissor, (int2)newScissor), min(
				int2(scissor.z, scissor.w), int2(newScissor.z, newScissor.w)));
		}
		parent = parentTransformView->getParent();
	}

	scissor.z = scissor.z - scissor.x; scissor.w = scissor.w - scissor.y;
	return clamp(scissor, int4::zero, int4(framebufferSize, framebufferSize));
}
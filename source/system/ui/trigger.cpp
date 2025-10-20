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

#include "garden/system/ui/trigger.hpp"
#include "garden/system/render/editor.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/profiler.hpp"

#include "math/matrix/transform.hpp"

using namespace garden;

//**********************************************************************************************************************
UiTriggerSystem::UiTriggerSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);

	ECSM_SUBSCRIBE_TO_EVENT("Update", UiTriggerSystem::update);
}
UiTriggerSystem::~UiTriggerSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);

		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", UiTriggerSystem::update);
	}
	unsetSingleton();
}

//**********************************************************************************************************************
static void triggerUiComponent(ID<Entity>& newElement, float& newPosZ, UiTriggerComponent& uiTriggerComp)
{
	auto entity = uiTriggerComp.getEntity();
	if (!entity)
		return;

	auto transformView = TransformSystem::Instance::get()->tryGetComponent(entity);
	if (!transformView || !transformView->isActive())
		return;

	auto inputSystem = InputSystem::Instance::get();
	auto model = transformView->calcModel(); auto modelPosZ = getTranslation(model).getZ();
	auto invModel = inverse4x4(model * scale(f32x4(uiTriggerComp.scale.x, uiTriggerComp.scale.y, 1.0f)));
	auto cursorPos = inputSystem->getCursorPosition() - (float2)inputSystem->getWindowSize() * 0.5f;
	auto modelCursorPos = float2(invModel * f32x4(cursorPos.x, cursorPos.y, 0.0f, 1.0f));

	if (abs(modelCursorPos.x) > 0.5f || abs(modelCursorPos.y) > 0.5f || modelPosZ >= newPosZ)
		return;
	newElement = uiTriggerComp.getEntity(); newPosZ = modelPosZ;
}

void UiTriggerSystem::update()
{
	SET_CPU_ZONE_SCOPED("UI Trigger Update");

	if (components.getCount() == 0)
		return;

	#if GARDEN_EDITOR
	auto wantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
	#else
	constexpr auto wantCaptureMouse = false;
	#endif

	auto inputSystem = InputSystem::Instance::get();
	if (wantCaptureMouse || !inputSystem->isCursorInWindow() || 
		inputSystem->getCursorMode() != CursorMode::Normal)
	{
		if (currElement)
		{
			auto uiTriggerView = getComponent(currElement);
			if (!uiTriggerView->onExit.empty())
				Manager::Instance::get()->tryRunEvent(uiTriggerView->onExit);
			currElement = {};
		}
		return;
	}

	auto componentData = components.getData();
	auto threadSystem = ThreadSystem::Instance::tryGet();
	ID<Entity> newElement = {}; float newPosZ = FLT_MAX;

	if (threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		newElements.assign(threadPool.getThreadCount(), make_pair(ID<Entity>(), FLT_MAX));
		auto newElementData = newElements.data();

		threadPool.addItems([componentData, newElementData](const ThreadPool::Task& task)
		{
			SET_CPU_ZONE_SCOPED("UI Trigger Update");

			auto itemCount = task.getItemCount();
			auto& threadElement = newElementData[task.getThreadIndex()];
			auto& newElement = threadElement.first; auto& newPosZ = threadElement.second;

			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				triggerUiComponent(newElement, newPosZ, componentData[i]);
		},
		components.getOccupancy());
		threadPool.wait();

		auto firstElement = newElements.begin();
		newElement = firstElement->first; newPosZ = firstElement->second;

		for (auto i = newElements.begin() + 1; i != newElements.end(); i++)
		{
			if (i->second >= newPosZ)
				continue;
			newElement = i->first; newPosZ = i->second;
		}
	}
	else
	{
		auto componentOccupancy = components.getOccupancy();
		for (uint32 i = 0; i < componentOccupancy; i++)
			triggerUiComponent(newElement, newPosZ, componentData[i]);
	}

	if (newElement)
	{
		auto manager = Manager::Instance::get();
		if (newElement == currElement)
		{
			auto uiTriggerView = getComponent(currElement);
			if (!uiTriggerView->onStay.empty())
				Manager::Instance::get()->tryRunEvent(uiTriggerView->onStay);
		}
		else
		{
			auto manager = Manager::Instance::get();
			if (currElement)
			{
				auto uiTriggerView = getComponent(currElement);
				if (!uiTriggerView->onExit.empty())
					manager->tryRunEvent(uiTriggerView->onExit);
			}
			currElement = newElement;

			auto uiTriggerView = getComponent(currElement);
			if (!uiTriggerView->onEnter.empty())
				manager->tryRunEvent(uiTriggerView->onEnter);
		}
	}
	else
	{
		if (currElement)
		{
			auto uiTriggerView = getComponent(currElement);
			if (!uiTriggerView->onExit.empty())
				Manager::Instance::get()->tryRunEvent(uiTriggerView->onExit);
			currElement = {};
		}
	}
}

void UiTriggerSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<UiTriggerComponent>(instance));
	if (currElement == component->getEntity())
		currElement = {};
	resetComponent(View<Component>(component), false);
	components.destroy(ID<UiTriggerComponent>(instance));
}
void UiTriggerSystem::resetComponent(View<Component> component, bool full)
{
	if (full)
	{
		auto uiTransformView = View<UiTriggerComponent>(component);
		uiTransformView->scale = float2::one;
		uiTransformView->onEnter = "";
		uiTransformView->onExit = "";
		uiTransformView->onStay = "";
	}
}
void UiTriggerSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<UiTriggerComponent>(source);
	auto destinationView = View<UiTriggerComponent>(destination);
	destinationView->scale = sourceView->scale;
	destinationView->onEnter = sourceView->onEnter;
	destinationView->onExit = sourceView->onExit;
	destinationView->onStay = sourceView->onStay;
}
string_view UiTriggerSystem::getComponentName() const
{
	return "Trigger UI";
}

//**********************************************************************************************************************
void UiTriggerSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto uiTransformView = View<UiTriggerComponent>(component);
	if (uiTransformView->scale != float2::one)
		serializer.write("scale", uiTransformView->scale);
	if (!uiTransformView->onEnter.empty())
		serializer.write("onEnter", uiTransformView->onEnter);
	if (!uiTransformView->onExit.empty())
		serializer.write("onExit", uiTransformView->onExit);
	if (!uiTransformView->onStay.empty())
		serializer.write("onStay", uiTransformView->onStay);
}
void UiTriggerSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto uiTransformView = View<UiTriggerComponent>(component);
	deserializer.read("scale", uiTransformView->scale);
	deserializer.read("onEnter", uiTransformView->onEnter);
	deserializer.read("onExit", uiTransformView->onExit);
	deserializer.read("onStay", uiTransformView->onStay);
}

//**********************************************************************************************************************
void UiTriggerSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto uiTriggerFrameView = View<UiTriggerFrame>(frame);
	if (uiTriggerFrameView->animateScale)
		serializer.write("scale", uiTriggerFrameView->scale);
}
ID<AnimationFrame> UiTriggerSystem::deserializeAnimation(IDeserializer& deserializer)
{
	UiTriggerFrame frame;
	frame.animateScale = deserializer.read("scale", frame.scale);

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}

void UiTriggerSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto uiTriggeriew = View<UiTriggerComponent>(component);
	const auto frameA = View<UiTriggerFrame>(a);
	const auto frameB = View<UiTriggerFrame>(b);

	if (frameA->animateScale)
		uiTriggeriew->scale = lerp(frameA->scale, frameB->scale, t);
}
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

#include "garden/system/animation.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/thread.hpp"
#include "garden/system/input.hpp"
#include "garden/profiler.hpp"

using namespace garden;

bool AnimationComponent::getActiveLooped(bool& isLooped) const
{
	if (active.empty())
		return false;

	auto searchResult = animations.find(active);
	if (searchResult == animations.end())
		return false;

	auto animation = AnimationSystem::Instance::get()->get(searchResult->second);
	isLooped = animation->isLooped;
	return true;
}

//**********************************************************************************************************************
AnimationSystem::AnimationSystem(bool animateAsync, bool setSingleton) : 
	Singleton(setSingleton), animateAsync(animateAsync)
{
	random_device randomDevice;
	this->randomGenerator = mt19937(randomDevice());

	ECSM_SUBSCRIBE_TO_EVENT("Update", AnimationSystem::update);
}
AnimationSystem::~AnimationSystem()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("Update", AnimationSystem::update);

	unsetSingleton();
}

//**********************************************************************************************************************
static void animateComponent(const AnimationSystem::Animations* animations, AnimationComponent& animationComp)
{
	auto entity = animationComp.getEntity();
	if (!entity || !animationComp.isPlaying || !animationComp.active.empty())
		return;

	auto transformView = Manager::Instance::get()->tryGet<TransformComponent>(entity);
	if (transformView)
	{
		if (!transformView->isActive())
			return;
	}

	const auto& componentAnimations = animationComp.getAnimations();
	auto searchResult = componentAnimations.find(animationComp.active);
	if (searchResult == componentAnimations.end())
		return;

	auto animationView = animations->get(searchResult->second);
	const auto& keyframes = animationView->getKeyframes();
	if (keyframes.empty())
		return;

	auto keyframeB = keyframes.lower_bound((int32)ceil(animationComp.frame));
	if (keyframeB == keyframes.end())
	{
		if (!animationView->isLooped && keyframes.size() > 1)
		{
			animationComp.isPlaying = false;
			return;
		}

		keyframeB--;
		animationComp.frame = keyframeB->first == 0 ? 0.0f : fmod(animationComp.frame, (float)keyframeB->first);
		keyframeB = keyframes.lower_bound((int32)ceil(animationComp.frame));
		GARDEN_ASSERT_MSG(keyframeB != keyframes.end(), "Something went wrong :(");
	}

	auto keyframeA = keyframeB;
	if (keyframeA != keyframes.begin())
		keyframeA--;

	auto manager = Manager::Instance::get();
	const auto& animatablesA = keyframeA->second;
	const auto& animatablesB = keyframeB->second;

	if (keyframeA == keyframeB)
	{
		for (const auto& pairA : animatablesA)
		{
			auto animatableSystem = dynamic_cast<IAnimatable*>(pairA.first);
			auto frameViewA = animatableSystem->getAnimation(pairA.second);
			auto componentView = manager->tryGet(entity, pairA.first->getComponentType());
			if (componentView)
				animatableSystem->animateAsync(componentView, frameViewA, frameViewA, 1.0f);
		}
	}
	else
	{
		auto currentFrame = animationComp.frame;
		auto frameA = keyframeA->first, frameB = keyframeB->first;
		auto frameDiff = frameB - frameA;
		auto t = frameDiff == 0 ? 1.0f : (currentFrame - frameA) / (float)frameDiff;

		for (const auto& pairA : animatablesA)
		{
			auto animationFrameB = animatablesB.at(pairA.first);
			auto animatableSystem = dynamic_cast<IAnimatable*>(pairA.first);
			auto frameViewA = animatableSystem->getAnimation(pairA.second);
			auto frameViewB = animatableSystem->getAnimation(animationFrameB);

			if (frameViewA->funcType == AnimationFunc::Pow)
				t = std::pow(t, frameViewA->coeff);
			else if (frameViewA->funcType == AnimationFunc::Gain)
				t = gain(t, frameViewA->coeff);

			auto componentView = manager->tryGet(entity, pairA.first->getComponentType());
			if (componentView)
				animatableSystem->animateAsync(componentView, frameViewA, frameViewB, t);
		}
	}

	if (!animationView->isLooped && keyframes.size() == 1)
	{
		animationComp.isPlaying = false;
		return;
	}

	animationComp.frame += InputSystem::Instance::get()->getDeltaTime() * animationView->frameRate;
}

//**********************************************************************************************************************
void AnimationSystem::update()
{
	SET_CPU_ZONE_SCOPED("Animations Update");

	if (components.getCount() == 0)
		return;

	auto animations = &this->animations;
	auto componentData = components.getData();
	auto threadSystem = ThreadSystem::Instance::tryGet();

	if (animateAsync && threadSystem)
	{
		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems([animations, componentData](const ThreadPool::Task& task)
		{
			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
				animateComponent(animations, componentData[i]);
		},
		components.getOccupancy());
		threadPool.wait();
	}
	else
	{
		auto componentOccupancy = components.getOccupancy();
		for (uint32 i = 0; i < componentOccupancy; i++)
			animateComponent(animations, componentData[i]);
	}
}

static void randomizeStartFrame(mt19937& randomGenerator, View<AnimationComponent> componentView)
{
	if (!componentView->randomizeStart || componentView->active.empty())
		return;

	const auto& animations = componentView->getAnimations();
	auto searchResult = animations.find(componentView->active);
	if (searchResult == animations.end())
		return;

	auto animationView = AnimationSystem::Instance::get()->get(searchResult->second);
	if (animationView->getKeyframes().empty())
		return;

	const auto& keyframe = animationView->getKeyframes().rbegin();
	if (keyframe->first != 0)
		componentView->frame = (float)(randomGenerator() / (double)(randomGenerator.max() / keyframe->first));
}

//**********************************************************************************************************************
void AnimationSystem::resetComponent(View<Component> component, bool full)
{
	auto animationView = View<AnimationComponent>(component);
	if (!animationView->animations.empty())
	{
		auto resourceSystem = ResourceSystem::Instance::get();
		for (const auto& pair : animationView->animations)
			resourceSystem->destroyShared(pair.second);
		animationView->animations.clear();
	}
}
void AnimationSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<AnimationComponent>(source);
	auto destinationView = View<AnimationComponent>(destination);
	destinationView->animations = sourceView->animations;
	destinationView->active = sourceView->active;
	destinationView->frame = sourceView->frame;
	destinationView->isPlaying = sourceView->isPlaying;
	destinationView->randomizeStart = sourceView->randomizeStart;
	randomizeStartFrame(randomGenerator, destinationView);
}
string_view AnimationSystem::getComponentName() const
{
	return "Animation";
}
void AnimationSystem::disposeComponents()
{
	components.dispose();
	animations.dispose();
}

//**********************************************************************************************************************
void AnimationSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto animationView = View<AnimationComponent>(component);
	const auto& animations = animationView->animations;

	if (!animations.empty())
	{
		serializer.beginChild("animations");
		for (const auto& pair : animations)
		{
			serializer.beginArrayElement();
			serializer.write(pair.first);
			serializer.endArrayElement();
		}
		serializer.endChild();
	}
	
	if (!animationView->active.empty())
		serializer.write("active", animationView->active);
	if (animationView->frame != 0.0f)
		serializer.write("frame", animationView->frame);
	if (!animationView->isPlaying)
		serializer.write("isPlaying", false);
	if (animationView->randomizeStart)
		serializer.write("randomizeStart", true);
}
void AnimationSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto animationView = View<AnimationComponent>(component);
	auto& animations = animationView->animations;

	if (deserializer.beginChild("animations"))
	{
		auto resourceSystem = ResourceSystem::Instance::get();
		auto arraySize = (uint32)deserializer.getArraySize();
		for (uint32 i = 0; i < arraySize; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			string path;
			deserializer.read(path);
			if (!path.empty())
			{
				auto animation = resourceSystem->loadAnimation(path, true);
				if (animation)
					animations.emplace(std::move(path), std::move(animation));
			}

			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	deserializer.read("active", animationView->active);
	deserializer.read("frame", animationView->frame);
	deserializer.read("isPlaying", animationView->isPlaying);
	deserializer.read("randomizeStart", animationView->randomizeStart);
	randomizeStartFrame(randomGenerator, animationView);
}
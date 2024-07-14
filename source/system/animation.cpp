// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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

using namespace garden;

//**********************************************************************************************************************
AnimationSystem* AnimationSystem::instance = nullptr;

AnimationSystem::AnimationSystem(bool animateAsync)
{
	this->animateAsync = animateAsync;

	auto manager = Manager::getInstance();
	SUBSCRIBE_TO_EVENT("Init", AnimationSystem::init);
	SUBSCRIBE_TO_EVENT("Update", AnimationSystem::update);

	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
AnimationSystem::~AnimationSystem()
{
	auto manager = Manager::getInstance();
	if (manager->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", AnimationSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Update", AnimationSystem::update);
	}

	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

void AnimationSystem::init()
{
	threadSystem = Manager::getInstance()->tryGet<ThreadSystem>();
}

//**********************************************************************************************************************
static void animateComponent(const LinearPool<Animation>* animations, AnimationComponent& animationComponent)
{
	auto entity = animationComponent.getEntity();
	if (!entity || !animationComponent.isPlaying && !animationComponent.active.empty())
		return;

	auto transformComponent = Manager::getInstance()->tryGet<TransformComponent>(entity);
	if (transformComponent)
	{
		if (!transformComponent->isActiveWithAncestors())
			return;
	}

	const auto& componentAnimations = animationComponent.getAnimations();
	auto searchResult = componentAnimations.find(animationComponent.active);
	if (searchResult == componentAnimations.end())
		return;

	auto animationView = animations->get(searchResult->second);
	const auto& keyframes = animationView->getKeyframes();
	if (keyframes.empty())
		return;

	auto keyframeB = keyframes.lower_bound((int32)ceil(animationComponent.frame));
	if (keyframeB == keyframes.end())
	{
		if (animationView->loop)
		{
			keyframeB--;
			animationComponent.frame = fmod(animationComponent.frame, (float)keyframeB->first);
			keyframeB = keyframes.lower_bound((int32)ceil(animationComponent.frame));
			GARDEN_ASSERT(keyframeB != keyframes.end()); // Something went wrong :(
		}
		else
		{
			animationComponent.isPlaying = false;
			return;
		}
	}

	auto keyframeA = keyframeB;
	if (keyframeA != keyframes.begin())
		keyframeA--;

	auto manager = Manager::getInstance();
	const auto& animatablesA = keyframeA->second;
	const auto& animatablesB = keyframeB->second;

	if (keyframeA == keyframeB)
	{
		for (const auto& pairA : animatablesA)
		{
			auto animatableSystem = dynamic_cast<IAnimatable*>(pairA.first);
			auto frameViewA = animatableSystem->getAnimation(pairA.second);
			auto component = manager->tryGet(entity, pairA.first->getComponentType());
			if (component)
				animatableSystem->animateAsync(component, frameViewA, frameViewA, 0.0f);
		}
	}
	else
	{
		auto currentFrame = animationComponent.frame;
		auto frameA = (float)keyframeA->first;
		auto frameB = (float)keyframeB->first;

		for (const auto& pairA : animatablesA)
		{
			auto animationFrameB = animatablesB.at(pairA.first);
			auto t = (currentFrame - frameA) / (frameB - frameA);
			auto animatableSystem = dynamic_cast<IAnimatable*>(pairA.first);
			auto frameViewA = animatableSystem->getAnimation(pairA.second);
			auto frameViewB = animatableSystem->getAnimation(animationFrameB);

			if (frameViewA->funcType == AnimationFunc::Pow)
				t = std::pow(t, frameViewA->coeff);
			else if (frameViewA->funcType == AnimationFunc::Gain)
				t = gain(t, frameViewA->coeff);

			auto component = manager->tryGet(entity, pairA.first->getComponentType());
			if (component)
				animatableSystem->animateAsync(component, frameViewA, frameViewB, t);
		}
	}

	animationComponent.frame += InputSystem::getInstance()->getDeltaTime() * animationView->frameRate;
}

//**********************************************************************************************************************
void AnimationSystem::update()
{
	auto animations = &this->animations;
	auto componentData = components.getData();
	auto occupancy = components.getOccupancy();

	if (animateAsync && threadSystem)
	{
		if (occupancy == 0)
			return;

		auto& threadPool = threadSystem->getForegroundPool();
		threadPool.addItems(ThreadPool::Task([&](const ThreadPool::Task& task)
		{
			auto itemCount = task.getItemCount();
			for (uint32 i = task.getItemOffset(); i < itemCount; i++)
			{
				auto& component = componentData[i];
				animateComponent(animations, component);
			}
		}),
		occupancy);
		threadPool.wait();
	}
	else
	{
		for (uint32 i = 0; i < occupancy; i++)
		{
			auto& component = componentData[i];
			animateComponent(animations, component);
		}
	}
}

//**********************************************************************************************************************
ID<Component> AnimationSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void AnimationSystem::destroyComponent(ID<Component> instance)
{
	auto component = components.get(ID<AnimationComponent>(instance));
	tryDestroyResources(component);
	components.destroy(ID<AnimationComponent>(instance));
}
void AnimationSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<AnimationComponent>(source);
	auto destinationView = View<AnimationComponent>(destination);
	destinationView->animations = sourceView->animations;
	destinationView->active = sourceView->active;
	destinationView->frame = sourceView->frame;
	destinationView->isPlaying = sourceView->isPlaying;
}
const string& AnimationSystem::getComponentName() const
{
	static const string name = "Animation";
	return name;
}
type_index AnimationSystem::getComponentType() const
{
	return typeid(AnimationComponent);
}
View<Component> AnimationSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<AnimationComponent>(instance)));
}
void AnimationSystem::disposeComponents()
{
	components.dispose();
	animations.dispose();
}

//**********************************************************************************************************************
void AnimationSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<AnimationComponent>(component);
	const auto& animations = componentView->animations;

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
	
	if (!componentView->active.empty())
		serializer.write("active", componentView->active);
	if (!componentView->frame != 0.0f)
		serializer.write("frame", componentView->frame);
	if (componentView->isPlaying != true)
		serializer.write("isPlaying", componentView->isPlaying);
}
void AnimationSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<AnimationComponent>(component);
	auto& animations = componentView->animations;

	if (deserializer.beginChild("animations"))
	{
		auto resourceSystem = ResourceSystem::getInstance();
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

	deserializer.read("active", componentView->active);
	deserializer.read("frame", componentView->frame);
	deserializer.read("isPlaying", componentView->isPlaying);
}

void AnimationSystem::tryDestroyResources(View<AnimationComponent> animationComponent)
{
	GARDEN_ASSERT(animationComponent);
	auto resourceSystem = ResourceSystem::getInstance();
	auto& animations = animationComponent->animations;
	for (const auto& pair : animations)
		resourceSystem->destroyShared(pair.second);
	animations.clear();
}
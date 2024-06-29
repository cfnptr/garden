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
#include "garden/system/resource.hpp"

using namespace garden;

//**********************************************************************************************************************
AnimationSystem* AnimationSystem::instance = nullptr;

AnimationSystem::AnimationSystem()
{
	GARDEN_ASSERT(!instance); // More than one system instance detected.
	instance = this;
}
AnimationSystem::~AnimationSystem()
{
	GARDEN_ASSERT(instance); // More than one system instance detected.
	instance = nullptr;
}

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
void AnimationSystem::copyComponent(ID<Component> source, ID<Component> destination)
{
	const auto sourceView = components.get(ID<AnimationComponent>(source));
	auto destinationView = components.get(ID<AnimationComponent>(destination));
	destinationView->animations = sourceView->animations;
	destinationView->active = sourceView->active;
	destinationView->frame = sourceView->frame;
	destinationView->isPlaying = sourceView->isPlaying;
}

//**********************************************************************************************************************
void AnimationSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto animationComponent = View<AnimationComponent>(component);
	const auto& animations = animationComponent->animations;

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
	
	if (!animationComponent->active.empty())
		serializer.write("active", animationComponent->active);
	if (!animationComponent->frame != 0)
		serializer.write("frame", animationComponent->frame);
	if (animationComponent->isPlaying != true)
		serializer.write("isPlaying", animationComponent->isPlaying);
}
void AnimationSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto animationComponent = View<AnimationComponent>(component);
	auto& animations = animationComponent->animations;

	if (deserializer.beginChild("animations"))
	{
		auto resourceSystem = ResourceSystem::getInstance();
		auto arraySize = deserializer.getArraySize();
		for (psize i = 0; i < arraySize; i++)
		{
			deserializer.beginArrayElement(i);

			string path;
			deserializer.read(path);
			if (!path.empty())
			{
				auto animation = resourceSystem->loadAnimation(path);
				if (animation)
					animations.emplace(std::move(path), std::move(animation));
			}

			deserializer.endArrayElement();
		}
		deserializer.endChild();
	}

	deserializer.read("active", animationComponent->active);
	deserializer.read("frame", animationComponent->frame);
	deserializer.read("isPlaying", animationComponent->isPlaying);
}

//**********************************************************************************************************************
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

void AnimationSystem::tryDestroyResources(View<AnimationComponent> animationComponent)
{
	GARDEN_ASSERT(animationComponent);
	auto& animations = animationComponent->animations;
	for (const auto& pair : animations)
	{
		if (pair.second.getRefCount() == 1)
			destroy(pair.second);
	}
	animations.clear();
}
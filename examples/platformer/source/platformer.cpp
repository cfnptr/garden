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

#include "platformer.hpp"
#include "garden/system/link.hpp"
#include "garden/system/spawner.hpp"
#include "garden/system/physics.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/2d-controller.hpp"
#include "garden/system/render/sprite/cutout.hpp"

#if GARDEN_EDITOR
#include "garden/system/render/editor.hpp"
#endif

using namespace platformer;

//**********************************************************************************************************************
PlatformerSystem::PlatformerSystem()
{
	SUBSCRIBE_TO_EVENT("Init", PlatformerSystem::init);
	SUBSCRIBE_TO_EVENT("Deinit", PlatformerSystem::deinit);
	SUBSCRIBE_TO_EVENT("Update", PlatformerSystem::update);
}
PlatformerSystem::~PlatformerSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", PlatformerSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", PlatformerSystem::deinit);
		UNSUBSCRIBE_FROM_EVENT("Update", PlatformerSystem::update);
	}
}

//**********************************************************************************************************************
void PlatformerSystem::init()
{
	#if GARDEN_EDITOR
	if (Manager::get()->has<EditorRenderSystem>())
	{
		SUBSCRIBE_TO_EVENT("EditorStart", PlatformerSystem::editorStart);
		SUBSCRIBE_TO_EVENT("EditorStop", PlatformerSystem::editorStop);
	}
	#endif

	ResourceSystem::get()->loadScene("platformer");

	auto physicsSystem = PhysicsSystem::get();
	auto items = LinkSystem::get()->findEntities("Item");

	for (auto i = items.first; i != items.second; i++)
	{
		auto rigidbodyView = physicsSystem->tryGet(i->second);
		if (rigidbodyView)
		{
			rigidbodyView->addListener<PlatformerSystem>([this](ID<Entity> thisEntity, ID<Entity> otherEntity)
			{
				onItemSensor(thisEntity, otherEntity);
			},
			BodyEvent::ContactAdded);
		}
	}

	auto characterSensors = LinkSystem::get()->findEntities("CharacterSensor");
	for (auto i = characterSensors.first; i != characterSensors.second; i++)
	{
		auto rigidbodyView = physicsSystem->tryGet(i->second);
		if (rigidbodyView)
		{
			rigidbodyView->addListener<PlatformerSystem>([this](ID<Entity> thisEntity, ID<Entity> otherEntity)
			{
				slideCounter++;
			},
			BodyEvent::ContactAdded);
			rigidbodyView->addListener<PlatformerSystem>([this](ID<Entity> thisEntity, ID<Entity> otherEntity)
			{
				slideCounter--;
			},
			BodyEvent::ContactRemoved);
		}
	}
}
void PlatformerSystem::deinit()
{
	#if GARDEN_EDITOR
	auto manager = Manager::get();
	if (manager->isRunning() && manager->has<EditorRenderSystem>())
	{
		UNSUBSCRIBE_FROM_EVENT("EditorStart", PlatformerSystem::editorStart);
		UNSUBSCRIBE_FROM_EVENT("EditorStop", PlatformerSystem::editorStop);
	}
	#endif
}

//**********************************************************************************************************************
void PlatformerSystem::update()
{
	auto manager = Manager::get();
	auto transformSystem = TransformSystem::get();
	auto characterSystem = CharacterSystem::get();
	auto controller2DSystem = manager->get<Controller2DSystem>();
	auto characters = LinkSystem::get()->findEntities("MainCharacter");

	for (auto i = characters.first; i != characters.second; i++)
	{
		auto entity = i->second;
		auto transformView = transformSystem->tryGet(entity);
		if (!transformView || !transformView->isActiveWithAncestors())
			continue;

		auto characterView = characterSystem->get(entity);
		if (!characterView || !characterView->getShape())
			continue;

		auto newState = currentState;
		auto linearVelocity = characterView->getLinearVelocity();

		if (length2(linearVelocity) > float3(0.01f))
		{
			if (characterView->getGroundState() == CharacterGround::OnGround)
			{
				newState = CharacterState::Run;
				isLastDoubleJumped = false;
			}
			else
			{
				if (controller2DSystem->isDoubleJumped() && !isLastDoubleJumped)
				{
					newState = CharacterState::DoubleJump;
					isLastDoubleJumped = true;
				}
				else
				{
					if (slideCounter > 0)
						newState = CharacterState::WallJump;
					else
						newState = linearVelocity.y > 0.0f ? CharacterState::Jump : CharacterState::Fall;
				}
			}
			isLastDirLeft = linearVelocity.x < 0.0f;
		}
		else
		{
			newState = CharacterState::Idle;
		}

		if (transformView->getChildCount() > 0)
		{
			auto child = transformView->getChild(0);
			auto spriteView = manager->tryGet<CutoutSpriteComponent>(child);
			if (spriteView)
				spriteView->uvSize.x = isLastDirLeft ? -1.0f : 1.0f;

			auto animationView = manager->tryGet<AnimationComponent>(child);
			if (animationView)
			{
				auto isLooped = false;
				if (animationView->isPlaying && animationView->getActiveLooped(isLooped))
				{
					if (!isLooped)
						newState = currentState;
				}

				if (newState != currentState)
				{
					if (newState == CharacterState::DoubleJump)
						animationView->frame = 0.0f;
					animationView->active = characterAnimStrings[(uint8)newState];
					animationView->isPlaying = true;
					currentState = newState;
				}
			}
		}
	}
}

#if GARDEN_EDITOR
//**********************************************************************************************************************
void PlatformerSystem::editorStart()
{
	auto spawner = LinkSystem::get()->findEntities("MainSpawner");
	if (spawner.first != spawner.second)
	{
		auto spawnerView = SpawnerSystem::get()->tryGet(spawner.first->second);
		if (spawnerView)
			spawnerView->spawn();
	}
}
void PlatformerSystem::editorStop()
{
	auto spawner = LinkSystem::get()->findEntities("MainSpawner");
	if (spawner.first != spawner.second)
	{
		auto spawnerView = SpawnerSystem::get()->tryGet(spawner.first->second);
		if (spawnerView)
			spawnerView->destroySpawned();
	}
}
#endif

//**********************************************************************************************************************
void PlatformerSystem::onItemSensor(ID<Entity> thisEntity, ID<Entity> otherEntity)
{
	auto transformSystem = TransformSystem::get();
	auto transformView = transformSystem->tryGet(thisEntity);
	if (transformView)
	{
		if (transformView->getParent())
			transformSystem->destroyRecursive(transformView->getParent());
		else
			transformSystem->destroyRecursive(thisEntity);
	}

	// TODO: spawn particles.
}
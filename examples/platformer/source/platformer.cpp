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
#include "garden/system/resource.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/animation.hpp"
#include "garden/system/character.hpp"
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

	auto manager = Manager::get();
	manager->registerEvent("Platformer.Item.Entered");
	manager->registerEvent("Platformer.Slide.Entered");
	manager->registerEvent("Platformer.Slide.Stayed");
	manager->registerEvent("Platformer.Slide.Exited");

	SUBSCRIBE_TO_EVENT("Platformer.Item.Entered", PlatformerSystem::itemEntered);
	SUBSCRIBE_TO_EVENT("Platformer.Slide.Entered", PlatformerSystem::slideEntered);
	SUBSCRIBE_TO_EVENT("Platformer.Slide.Stayed", PlatformerSystem::slideStayed);
	SUBSCRIBE_TO_EVENT("Platformer.Slide.Exited", PlatformerSystem::slideExited);
}
PlatformerSystem::~PlatformerSystem()
{
	if (Manager::get()->isRunning())
	{
		UNSUBSCRIBE_FROM_EVENT("Init", PlatformerSystem::init);
		UNSUBSCRIBE_FROM_EVENT("Deinit", PlatformerSystem::deinit);
		UNSUBSCRIBE_FROM_EVENT("Update", PlatformerSystem::update);

		auto manager = Manager::get();
		manager->unregisterEvent("Platformer.Item.Entered");
		manager->unregisterEvent("Platformer.Slide.Entered");
		manager->unregisterEvent("Platformer.Slide.Stayed");
		manager->unregisterEvent("Platformer.Slide.Exited");

		UNSUBSCRIBE_FROM_EVENT("Platformer.Item.Entered", PlatformerSystem::itemEntered);
		UNSUBSCRIBE_FROM_EVENT("Platformer.Slide.Entered", PlatformerSystem::slideEntered);
		UNSUBSCRIBE_FROM_EVENT("Platformer.Slide.Stayed", PlatformerSystem::slideStayed);
		UNSUBSCRIBE_FROM_EVENT("Platformer.Slide.Exited", PlatformerSystem::slideExited);
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
	auto inputSystem = InputSystem::get();
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

		auto characterView = characterSystem->tryGet(entity);
		if (!characterView || !characterView->getShape())
			continue;

		auto newState = currentState;
		auto linearVelocity = characterView->getLinearVelocity();
		if (slideCounter > 0 && linearVelocity.y < -maxSlideSpeed)
		{
			linearVelocity.y = -maxSlideSpeed;
			characterView->setLinearVelocity(linearVelocity);
		}

		if (length2(linearVelocity) > float3(0.1f * 0.1f))
		{
			auto groundState = characterView->getGroundState();

			if (groundState == CharacterGround::OnGround)
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
					if (linearVelocity.y > 0.0f)
						newState = CharacterState::Jump;
					else
						newState = slideCounter > 0 ? CharacterState::WallJump : CharacterState::Fall;
				}
			}

			if (slideCounter <= 0)
				isLastDirLeft = linearVelocity.x < 0.0f;
		}
		else
		{
			newState = CharacterState::Idle;
		}

		auto childCount = transformView->getChildCount();
		for (uint32 i = 0; i < childCount; i++)
		{
			auto child = transformView->getChild(i);

			auto spriteView = manager->tryGet<CutoutSpriteComponent>(child);
			if (spriteView)
				spriteView->uvSize.x = isLastDirLeft ? -1.0f : 1.0f;

			auto animationView = manager->tryGet<AnimationComponent>(child);
			if (animationView)
			{
				auto isLooped = false;
				if (animationView->isPlaying && animationView->getActiveLooped(isLooped) && !isLooped)
					newState = currentState;

				if (newState != currentState)
				{
					animationView->active = characterAnimStrings[(uint8)newState];
					if (animationView->getActiveLooped(isLooped) && !isLooped)
						animationView->frame = 0.0f;
					animationView->isPlaying = true;
					currentState = newState;
				}
			}

			auto rigidbodyView = manager->tryGet<RigidbodyComponent>(child);
			if (rigidbodyView && rigidbodyView->getShape())
			{
				float3 position; quat rotation;
				characterView->getPosAndRot(position, rotation);
				if (rigidbodyView->isPosAndRotChanged(position, rotation))
					rigidbodyView->setPosAndRot(position, rotation);
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
		auto entity = spawner.first->second;
		auto spawnerView = SpawnerSystem::get()->tryGet(entity);
		if (spawnerView)
			spawnerView->spawn();

		auto transformView = TransformSystem::get()->tryGet(entity);
		if (transformView)
		{
			auto childCount = transformView->getChildCount();
			for (uint32 i = 0; i < childCount; i++)
			{
				auto child = transformView->getChild(i);
				auto animationView = AnimationSystem::get()->tryGet(child);
				if (!animationView)
					continue;

				currentState = CharacterState::Appearing;
				animationView->active = characterAnimStrings[(uint8)CharacterState::Appearing];
				animationView->frame = 0;
				animationView->isPlaying = true;
			}
		}
	}

	auto characters = LinkSystem::get()->findEntities("MainCharacter");
	for (auto i = characters.first; i != characters.second; i++)
	{
		auto entity = i->second;
		auto transformView = TransformSystem::get()->tryGet(entity);
		if (!transformView || !transformView->isActiveWithAncestors())
			continue;

		auto childCount = transformView->getChildCount();
		for (uint32 i = 0; i < childCount; i++)
		{
			auto child = transformView->getChild(i);
			auto animationView = AnimationSystem::get()->tryGet(child);
			if (!animationView)
				continue;

			currentState = CharacterState::Appearing;
			animationView->active = characterAnimStrings[(uint8)CharacterState::Appearing];
			animationView->frame = 0;
			animationView->isPlaying = true;
		}
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
	slideCounter = 0;
}
#endif

//**********************************************************************************************************************
void PlatformerSystem::itemEntered()
{
	auto physicsSystem = PhysicsSystem::get();
	auto transformSystem = TransformSystem::get();
	auto transformView = transformSystem->tryGet(physicsSystem->getThisBody());

	if (transformView)
	{
		if (transformView->getParent())
			transformSystem->destroyRecursive(transformView->getParent());
		else
			transformSystem->destroyRecursive(physicsSystem->getThisBody());
	}

	// TODO: spawn particles.
}
void PlatformerSystem::slideEntered()
{
	slideCounter++;
}
void PlatformerSystem::slideStayed()
{
	auto physicsSystem = PhysicsSystem::get();
	auto rigidbodyView = physicsSystem->get(physicsSystem->getThisBody());
	auto shapeView = physicsSystem->get(rigidbodyView->getShape());
	if (shapeView->getSubType() == ShapeSubType::RotatedTranslated)
	{
		auto position = shapeView->getPosition();
		isLastDirLeft = position.x < 0.0f;
	}
}
void PlatformerSystem::slideExited()
{
	slideCounter--;
}
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
static void setTagEntityActive(const string& tag, bool isActive, bool isDirLeft)
{
	auto manager = Manager::get();
	auto transformSystem = TransformSystem::get();
	auto entities = LinkSystem::get()->findEntities(tag);

	for (auto i = entities.first; i != entities.second; i++)
	{
		auto entity = i->second;
		auto transformView = transformSystem->tryGet(entity);
		if (!transformView || !transformView->getParent())
			continue;

		auto parentView = transformSystem->tryGet(transformView->getParent());
		if (!parentView || !parentView->isActiveWithAncestors())
			continue;

		transformView->isActive = isActive;

		auto spriteView = manager->tryGet<CutoutSpriteComponent>(entity);
		if (spriteView)
			spriteView->uvSize.x = isDirLeft ? -1.0f : 1.0f;
	}
}

void PlatformerSystem::update()
{
	auto manager = Manager::get();
	auto transformSystem = TransformSystem::get();
	auto characterSystem = CharacterSystem::get();
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
				newState = CharacterState::Run;
			else
				newState = linearVelocity.y > 0.0f ? CharacterState::Jump : CharacterState::Fall;
			isLastDirLeft = linearVelocity.x < 0.0f;
		}
		else
		{
			newState = CharacterState::Idle;
		}

		if (newState != currentState)
		{
			setTagEntityActive(characterStateStrings[(uint8)currentState], false, isLastDirLeft);
			setTagEntityActive(characterStateStrings[(uint8)newState], true, isLastDirLeft);
			currentState = newState;
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
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


#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"

namespace platformer
{

using namespace ecsm;
using namespace garden;

enum class CharacterState : uint8
{
	Idle, Run, Jump, Fall, Count
};
static const string characterStateStrings[(uint8)CharacterState::Count] =
{
	"CharacterIdle", "CharacterRun", "CharacterJump", "CharacterFall"
};

class PlatformerSystem final : public System
{
	CharacterState currentState = {};
	bool isLastDirLeft = false;

	PlatformerSystem();
	~PlatformerSystem() final;

	void init();
	void deinit();
	void update();

	#if GARDEN_EDITOR
	void editorStart();
	void editorStop();
	#endif
	
	void onItemSensor(ID<Entity> thisEntity, ID<Entity> otherEntity);
	friend class ecsm::Manager;
};

} // namespace garden
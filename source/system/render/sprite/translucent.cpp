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

#include "garden/system/render/sprite/translucent.hpp"

using namespace garden;

//**********************************************************************************************************************
TransSpriteSystem::TransSpriteSystem(bool setSingleton) : SpriteRenderCompSystem(
	"sprite/translucent"), Singleton(setSingleton) { }
TransSpriteSystem::~TransSpriteSystem() { unsetSingleton(); }

const string& TransSpriteSystem::getComponentName() const
{
	static const string name = "Translucent Sprite";
	return name;
}
MeshRenderType TransSpriteSystem::getMeshRenderType() const
{
	return MeshRenderType::Translucent;
}
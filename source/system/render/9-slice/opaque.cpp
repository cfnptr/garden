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

#include "garden/system/render/9-slice/opaque.hpp"

using namespace garden;

//**********************************************************************************************************************
Opaque9SliceSystem::Opaque9SliceSystem(bool setSingleton) : 
	NineSliceRenderCompSystem("9-slice/opaque"), Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<IMeshRenderSystem>(this);
}
Opaque9SliceSystem::~Opaque9SliceSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<IMeshRenderSystem>(this);
	unsetSingleton();
}

string_view Opaque9SliceSystem::getComponentName() const
{
	return "Opaque 9-Slice";
}
MeshRenderType Opaque9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::Opaque;
}
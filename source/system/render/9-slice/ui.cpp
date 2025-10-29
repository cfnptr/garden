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

#include "garden/system/render/9-slice/ui.hpp"

using namespace garden;

//**********************************************************************************************************************
Ui9SliceSystem::Ui9SliceSystem(bool setSingleton) : 
	NineSliceCompAnimSystem("9-slice/translucent"), Singleton(setSingleton)
{
	Manager::Instance::get()->addGroupSystem<IMeshRenderSystem>(this);
}
Ui9SliceSystem::~Ui9SliceSystem()
{
	if (Manager::Instance::get()->isRunning)
		Manager::Instance::get()->removeGroupSystem<IMeshRenderSystem>(this);
	unsetSingleton();
}

string_view Ui9SliceSystem::getComponentName() const
{
	return "9-Slice UI";
}
MeshRenderType Ui9SliceSystem::getMeshRenderType() const
{
	return MeshRenderType::UI;
}
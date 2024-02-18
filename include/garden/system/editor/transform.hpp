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
#include "garden/system/transform.hpp"

#if GARDEN_EDITOR
namespace garden
{

class TransformEditor final
{
	TransformSystem* system = nullptr;
	float3 oldEulerAngles = float3(0.0f);
	float3 newEulerAngles = float3(0.0f);
	quat oldRotation = quat::identity;
	ID<Entity> selectedEntity = {};

	TransformEditor(TransformSystem* system);
	
	void onDestroy(ID<Entity> entity);
	void onEntityInspector(ID<Entity> entity);

	friend class TransformSystem;
};

} // namespace garden
#endif
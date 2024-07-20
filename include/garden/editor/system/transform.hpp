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
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
namespace garden
{

class TransformEditorSystem final : public System
{
	float3 oldEulerAngles = float3(0.0f);
	float3 newEulerAngles = float3(0.0f);
	quat oldRotation = quat::identity;
	ID<Entity> selectedEntity = {};

	static TransformEditorSystem* instance;

	TransformEditorSystem();
	~TransformEditorSystem() final;
	
	void init();
	void deinit();

	void onEntityDestroy(ID<Entity> entity);
	void onEntityInspector(ID<Entity> entity, bool isOpened);

	friend class ecsm::Manager;
	friend class TransformSystem;
public:
	float inspectorPriority = -0.9f;

	static TransformEditorSystem* getInstance() noexcept
	{
		GARDEN_ASSERT(instance); // System is not created.
		return instance;
	}
};

} // namespace garden
#endif
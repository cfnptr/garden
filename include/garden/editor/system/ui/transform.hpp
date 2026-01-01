// Copyright 2022-2026 Nikita Fediuchin. All rights reserved.
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

class UiTransformEditorSystem final : public System, public Singleton<UiTransformEditorSystem>
{
	f32x4 oldEulerAngles = f32x4::zero;
	f32x4 newEulerAngles = f32x4::zero;
	quat oldRotation = quat::identity;
	ID<Entity> selectedEntity = {};

	UiTransformEditorSystem(bool setSingleton = true);
	~UiTransformEditorSystem() final;
	
	void init();
	void deinit();

	void onEntityDestroy(ID<Entity> entity);
	void onEntityInspector(ID<Entity> entity, bool isOpened);

	friend class ecsm::Manager;
public:
	float inspectorPriority = 0.8f;
};

} // namespace garden
#endif
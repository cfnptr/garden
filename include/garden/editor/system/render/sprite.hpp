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
#include "garden/system/render/sprite.hpp"

namespace garden
{

class SpriteRenderEditorSystem final : public System
{
	SpriteRenderEditorSystem();
	~SpriteRenderEditorSystem() final;

	void init();
	void deinit();

	void onOpaqueEntityInspector(ID<Entity> entity, bool isOpened);
	void onCutoutEntityInspector(ID<Entity> entity, bool isOpened);
	void onTransEntityInspector(ID<Entity> entity, bool isOpened);
	void onUiEntityInspector(ID<Entity> entity, bool isOpened);

	friend class ecsm::Manager;
public:
	static void renderComponent(SpriteRenderComponent* component, type_index componentType);
};

} // namespace garden
#endif
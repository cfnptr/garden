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

#pragma once
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
namespace garden
{

class InfiniteGridEditorSystem final : public System
{
public:
	struct PushConstants final
	{
		float4 meshColor;
		float4 axisColorX;
		float4 axisColorYZ;
		float meshScale;
		int32 isHorizontal;
	};
private:
	ID<GraphicsPipeline> pipeline;
	ID<DescriptorSet> descriptorSet;

	InfiniteGridEditorSystem();
	~InfiniteGridEditorSystem() final;

	void init();
	void deinit();
	void preRender();
	void render();
	void editorSettings();
	
	friend class ecsm::Manager;
public:
	Color meshColor = Color("101010FF");
	Color axisColorX = Color("FF1010FF");
	Color axisColorYZ = Color("1010FFFF");
	float meshScale = 1.0f;
	bool isHorizontal = true;
	bool isEnabled = true;
};

} // namespace garden
#endif
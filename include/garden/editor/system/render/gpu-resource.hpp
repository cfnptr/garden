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

class GpuResourceEditorSystem final : public System
{
public:
	enum class TabType : uint8
	{
		None, Buffers, Images, ImageViews, Framebuffers, DescriptorSets,
		GraphicsPipelines, ComputePipelines, Count
	};
private:
	string searchString;
	uint32 selectedItem = 0;
	bool showWindow = false;
	bool searchCaseSensitive = false;
	TabType openNextTab = TabType::None;

	GpuResourceEditorSystem();
	~GpuResourceEditorSystem() final;

	void init();
	void deinit();
	void editorRender();
	void editorBarTool();

	friend class ecsm::Manager;
public:
	void openTab(ID<Resource> resource, TabType type) noexcept;

	void openTab(ID<Buffer> buffer) noexcept
	{ openTab(ID<Resource>(buffer), TabType::Buffers); }
	void openTab(ID<Image> image) noexcept
	{ openTab(ID<Resource>(image), TabType::Images); }
	void openTab(ID<ImageView> imageView) noexcept
	{ openTab(ID<Resource>(imageView), TabType::ImageViews); }
	void openTab(ID<Framebuffer> framebuffer) noexcept
	{ openTab(ID<Resource>(framebuffer), TabType::Framebuffers); }
	void openTab(ID<DescriptorSet> descriptrSet) noexcept
	{ openTab(ID<Resource>(descriptrSet), TabType::DescriptorSets); }
	void openTab(ID<GraphicsPipeline> graphicsPipeline) noexcept
	{ openTab(ID<Resource>(graphicsPipeline), TabType::GraphicsPipelines); }
	void openTab(ID<ComputePipeline> computePipeline) noexcept
	{ openTab(ID<Resource>(computePipeline), TabType::ComputePipelines); }
};

} // namespace garden
#endif
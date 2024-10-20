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

#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace garden::graphics;

//**********************************************************************************************************************
static vk::PrimitiveTopology toVkPrimitiveTopology(GraphicsPipeline::Topology topology)
{
	switch (topology)
	{
	case GraphicsPipeline::Topology::TriangleList: return vk::PrimitiveTopology::eTriangleList;
	case GraphicsPipeline::Topology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
	case GraphicsPipeline::Topology::LineList: return vk::PrimitiveTopology::eLineList;
	case GraphicsPipeline::Topology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
	case GraphicsPipeline::Topology::PointList: return vk::PrimitiveTopology::ePointList;
	default: abort();
	}
}
static vk::PolygonMode toVkPolygonMode(GraphicsPipeline::Polygon polygon)
{
	switch (polygon)
	{
	case GraphicsPipeline::Polygon::Fill: return vk::PolygonMode::eFill;
	case GraphicsPipeline::Polygon::Line: return vk::PolygonMode::eLine;
	case GraphicsPipeline::Polygon::Point: return vk::PolygonMode::ePoint;
	default: abort();
	}
}
static vk::CullModeFlags toVkCullMode(GraphicsPipeline::CullFace cullFace)
{
	switch (cullFace)
	{
	case GraphicsPipeline::CullFace::Front: return vk::CullModeFlagBits::eFront;
	case GraphicsPipeline::CullFace::Back: return vk::CullModeFlagBits::eBack;
	case GraphicsPipeline::CullFace::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
	default: abort();
	}
}
static vk::FrontFace toVkFrontFace(GraphicsPipeline::FrontFace frontFace)
{
	switch (frontFace)
	{
	case GraphicsPipeline::FrontFace::Clockwise: return vk::FrontFace::eClockwise;
	case GraphicsPipeline::FrontFace::CounterClockwise: return vk::FrontFace::eCounterClockwise;
	default: abort();
	}
}

//**********************************************************************************************************************
static vk::BlendFactor toVkBlendFactor(GraphicsPipeline::BlendFactor blendFactor)
{
	switch (blendFactor)
	{
	case GraphicsPipeline::BlendFactor::Zero: return vk::BlendFactor::eZero;
	case GraphicsPipeline::BlendFactor::One: return vk::BlendFactor::eOne;
	case GraphicsPipeline::BlendFactor::SrcColor: return vk::BlendFactor::eSrcColor;
	case GraphicsPipeline::BlendFactor::OneMinusSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
	case GraphicsPipeline::BlendFactor::DstColor: return vk::BlendFactor::eDstColor;
	case GraphicsPipeline::BlendFactor::OneMinusDstColor: return vk::BlendFactor::eOneMinusDstColor;
	case GraphicsPipeline::BlendFactor::SrcAlpha: return vk::BlendFactor::eSrcAlpha;
	case GraphicsPipeline::BlendFactor::OneMinusSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
	case GraphicsPipeline::BlendFactor::DstAlpha: return vk::BlendFactor::eDstAlpha;
	case GraphicsPipeline::BlendFactor::OneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
	case GraphicsPipeline::BlendFactor::ConstColor: return vk::BlendFactor::eConstantColor;
	case GraphicsPipeline::BlendFactor::OneMinusConstColor: return vk::BlendFactor::eOneMinusConstantColor;
	case GraphicsPipeline::BlendFactor::ConstAlpha: return vk::BlendFactor::eConstantAlpha;
	case GraphicsPipeline::BlendFactor::OneMinusConstAlpha: return vk::BlendFactor::eOneMinusConstantAlpha;
	case GraphicsPipeline::BlendFactor::Src1Color: return vk::BlendFactor::eSrc1Color;
	case GraphicsPipeline::BlendFactor::OneMinusSrc1Color: return vk::BlendFactor::eOneMinusSrc1Color;
	case GraphicsPipeline::BlendFactor::Src1Alpha: return vk::BlendFactor::eSrc1Alpha;
	case GraphicsPipeline::BlendFactor::SrcAlphaSaturate: return vk::BlendFactor::eSrcAlphaSaturate;
	default: abort();
	}
}
static vk::BlendOp toVkBlendOp(GraphicsPipeline::BlendOperation blendOperation)
{
	switch (blendOperation)
	{
	case GraphicsPipeline::BlendOperation::Add: return vk::BlendOp::eAdd;
	case GraphicsPipeline::BlendOperation::Subtract: return vk::BlendOp::eSubtract;
	case GraphicsPipeline::BlendOperation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
	case GraphicsPipeline::BlendOperation::Minimum: return vk::BlendOp::eMin;
	case GraphicsPipeline::BlendOperation::Maximum: return vk::BlendOp::eMax;
	default: abort();
	}
}
static vk::ColorComponentFlags toVkColorComponents(GraphicsPipeline::ColorComponent colorComponents)
{
	vk::ColorComponentFlags result;
	if (hasAnyFlag(colorComponents, GraphicsPipeline::ColorComponent::R))
		result |= vk::ColorComponentFlagBits::eR;
	if (hasAnyFlag(colorComponents, GraphicsPipeline::ColorComponent::G))
		result |= vk::ColorComponentFlagBits::eG;
	if (hasAnyFlag(colorComponents, GraphicsPipeline::ColorComponent::B))
		result |= vk::ColorComponentFlagBits::eB;
	if (hasAnyFlag(colorComponents, GraphicsPipeline::ColorComponent::A))
		result |= vk::ColorComponentFlagBits::eA;
	return result;
}

//**********************************************************************************************************************
GraphicsPipeline::GraphicsPipeline(GraphicsCreateData& createData, bool asyncRecording) :
	Pipeline(createData, asyncRecording)
{
	if (createData.variantCount > 1)
		this->instance = malloc<vk::Pipeline>(createData.variantCount);

	vector<ShaderStage> stages; vector<vector<uint8>> code;
	if (!createData.vertexCode.empty())
	{
		stages.push_back(ShaderStage::Vertex);
		code.push_back(std::move(createData.vertexCode));
		
	}
	if (!createData.fragmentCode.empty())
	{
		stages.push_back(ShaderStage::Fragment);
		code.push_back(std::move(createData.fragmentCode));
	}

	auto shaders = createShaders(code, createData.shaderPath);
	vector<vk::PipelineShaderStageCreateInfo> stageInfos(stages.size());
	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.pName = "main";

	vector<vk::SpecializationInfo> specializationInfos(stages.size());
	for (uint32 i = 0; i < (uint32)stages.size(); i++)
	{
		auto stage = stages[i];
		auto specializationInfo = &specializationInfos[i];

		fillSpecConsts(createData.shaderPath, stage, createData.variantCount,
			specializationInfo, createData.specConsts, createData.specConstValues);

		stageInfo.stage = toVkShaderStage(stage);
		stageInfo.module = (VkShaderModule)shaders[i];
		stageInfo.pSpecializationInfo = specializationInfo->mapEntryCount > 0 ? specializationInfo : nullptr;
		stageInfos[i] = stageInfo;
	}

	vk::GraphicsPipelineCreateInfo pipelineInfo({}, (uint32)stageInfos.size(), stageInfos.data(),
		nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
		(VkPipelineLayout)pipelineLayout, nullptr, createData.subpassIndex, nullptr, -1);

	vk::PipelineRenderingCreateInfoKHR dynamicRenderingInfo;
	vector<vk::Format> dynamicColorFormats;

	if (!createData.renderPass)
	{
		const auto& colorFormats = createData.colorFormats;

		// Different shader output and framebuffer attachment count.
		GARDEN_ASSERT(createData.blendStates.size() == colorFormats.size()); 

		if (!colorFormats.empty())
		{
			dynamicColorFormats.resize(colorFormats.size());
			for (uint32 i = 0; i < (uint32)colorFormats.size(); i++)
				dynamicColorFormats[i] = toVkFormat(colorFormats[i]);

			dynamicRenderingInfo.colorAttachmentCount = (uint32)dynamicColorFormats.size();
			dynamicRenderingInfo.pColorAttachmentFormats = dynamicColorFormats.data();
		}

		if (createData.depthStencilFormat != Image::Format::Undefined)
		{
			if (isFormatDepthOnly(createData.depthStencilFormat))
			{
				dynamicRenderingInfo.depthAttachmentFormat = toVkFormat(createData.depthStencilFormat);
				dynamicRenderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;
			}
			else if (isFormatStencilOnly(createData.depthStencilFormat))
			{
				dynamicRenderingInfo.stencilAttachmentFormat = toVkFormat(createData.depthStencilFormat);
				dynamicRenderingInfo.depthAttachmentFormat = vk::Format::eUndefined;
			}
			else
			{
				dynamicRenderingInfo.depthAttachmentFormat = 
					dynamicRenderingInfo.stencilAttachmentFormat = toVkFormat(createData.depthStencilFormat);
			}
		}
		else
		{
			dynamicRenderingInfo.depthAttachmentFormat = 
				dynamicRenderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;
		}
		
		pipelineInfo.pNext = &dynamicRenderingInfo;
	}
	else
	{
		pipelineInfo.renderPass = (VkRenderPass)createData.renderPass;
	}

	const auto& stateOverrides = createData.stateOverrides;
	for (uint32 variantIndex = 0; variantIndex < createData.variantCount; variantIndex++)
	{
		auto stateSearch = stateOverrides.find(variantIndex);
		const auto& pipelineState = stateSearch == stateOverrides.end() ?
			createData.pipelineState : stateSearch->second;

		vk::VertexInputBindingDescription bindingDescription;
		vk::PipelineVertexInputStateCreateInfo inputInfo;
		vector<vk::VertexInputAttributeDescription> inputAttributes(createData.vertexAttributes.size());

		if (!createData.vertexAttributes.empty())
		{
			auto vertexAttributes = createData.vertexAttributes.data();
			for (uint32 i = 0; i < (uint32)inputAttributes.size(); i++)
			{
				auto attribute = vertexAttributes[i];
				auto& inputAttribute = inputAttributes[i];
				inputAttribute.location = i;
				inputAttribute.binding = 0;
				inputAttribute.format = toVkFormat(attribute.type, attribute.format);
				inputAttribute.offset = attribute.offset;
			}

			bindingDescription.stride = createData.vertexAttributesSize;
			bindingDescription.inputRate = vk::VertexInputRate::eVertex;
			inputInfo.vertexBindingDescriptionCount = 1;
			inputInfo.pVertexBindingDescriptions = &bindingDescription;
			inputInfo.vertexAttributeDescriptionCount = (uint32)inputAttributes.size();
			inputInfo.pVertexAttributeDescriptions = inputAttributes.data();
			// TODO: allow to specify input rate for an each vertex attribute?
		}

		vk::PipelineInputAssemblyStateCreateInfo assemblyInfo({},
			toVkPrimitiveTopology(pipelineState.topology), VK_FALSE); // TODO: support primitive restarting

		vk::PipelineViewportStateCreateInfo viewportInfo;
		viewportInfo.viewportCount = 1; // TODO: pass it as argument.
		viewportInfo.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizationInfo({},
			pipelineState.depthClamping, pipelineState.discarding, toVkPolygonMode(pipelineState.polygon),
			pipelineState.faceCulling ? toVkCullMode(pipelineState.cullFace) : vk::CullModeFlagBits::eNone,
			toVkFrontFace(pipelineState.frontFace), pipelineState.depthBiasing, pipelineState.depthBiasConstant,
			pipelineState.depthBiasClamp, pipelineState.depthBiasSlope, 1.0f);
		vk::PipelineMultisampleStateCreateInfo multisampleInfo({},
			vk::SampleCountFlagBits::e1, VK_FALSE, 1.0f, nullptr, VK_FALSE, VK_FALSE);

		vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
		if (createData.depthStencilFormat != Image::Format::Undefined)
		{
			depthStencilInfo.depthTestEnable = pipelineState.depthTesting;
			depthStencilInfo.depthWriteEnable = pipelineState.depthWriting;
			depthStencilInfo.depthCompareOp = toVkCompareOp(pipelineState.depthCompare);
			depthStencilInfo.minDepthBounds = 0.0f;
			depthStencilInfo.maxDepthBounds = 1.0f;
			// TODO: stencil testing, depth boundsTesting
		}

		auto blendStates = createData.blendStates.data();
		vector<vk::PipelineColorBlendAttachmentState> blendAttachments(createData.blendStates.size());

		for (uint32 i = 0; i < (uint32)blendAttachments.size(); i++)
		{
			auto blendState = blendStates[i];
			blendAttachments[i] = vk::PipelineColorBlendAttachmentState(blendState.blending,
				toVkBlendFactor(blendState.srcColorFactor), toVkBlendFactor(blendState.dstColorFactor),
				toVkBlendOp(blendState.colorOperation), toVkBlendFactor(blendState.srcAlphaFactor),
				toVkBlendFactor(blendState.dstAlphaFactor), toVkBlendOp(blendState.alphaOperation),
				toVkColorComponents(blendState.colorMask));
		}

		array<float, 4> blendConstant =
		{
			pipelineState.blendConstant.x, pipelineState.blendConstant.y,
			pipelineState.blendConstant.z, pipelineState.blendConstant.w
		};

		vk::PipelineColorBlendStateCreateInfo blendInfo({}, VK_FALSE, {},
			(uint32)blendAttachments.size(), blendAttachments.data(), blendConstant); // TODO: logical operations
		
		// TODO: pass dynamic states as argument.
		// Also allow to specify static viewport and scissor.
		vector<vk::DynamicState> dynamicStates; 
		dynamicStates.push_back(vk::DynamicState::eViewport);
		dynamicStates.push_back(vk::DynamicState::eScissor);
		vk::PipelineDynamicStateCreateInfo dynamicInfo;

		if (!dynamicStates.empty())
		{
			dynamicInfo.dynamicStateCount = (uint32)dynamicStates.size();
			dynamicInfo.pDynamicStates = dynamicStates.data();
			pipelineInfo.pDynamicState = &dynamicInfo;
		}

		pipelineInfo.pVertexInputState = &inputInfo;
		pipelineInfo.pInputAssemblyState = &assemblyInfo;
		pipelineInfo.pViewportState = &viewportInfo;
		pipelineInfo.pRasterizationState = &rasterizationInfo;
		pipelineInfo.pMultisampleState = &multisampleInfo;
		pipelineInfo.pDepthStencilState = &depthStencilInfo;
		pipelineInfo.pColorBlendState = &blendInfo;

		auto result = Vulkan::device.createGraphicsPipeline(Vulkan::pipelineCache, pipelineInfo);
		vk::detail::resultCheck(result.result, "vk::Device::createGraphicsPipeline");

		if (createData.variantCount > 1)
			((void**)this->instance)[variantIndex] = result.value;
		else
			this->instance = result.value;
	}

	for (const auto& info : specializationInfos)
	{
		free((void*)info.pMapEntries);
		free((void*)info.pData);
	}

	destroyShaders(shaders);
}

//**********************************************************************************************************************
void GraphicsPipeline::updateFramebuffer(ID<Framebuffer> framebuffer)
{
	GARDEN_ASSERT(framebuffer);
	#if GARDEN_DEBUG
	auto framebufferView = GraphicsAPI::framebufferPool.get(framebuffer);
	GARDEN_ASSERT(framebufferView->getSubpasses().empty());
	#endif
	this->framebuffer = framebuffer;
}

void GraphicsPipeline::setViewport(const float4& viewport)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	// TODO: support multiple viewport/scissor count. MacBook intel viewport max count is 16.
	SetViewportCommand command;
	if (viewport == float4(0.0f))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		command.viewport = float4(float2(0.0f), framebufferView->getSize());
	}
	else
	{
		command.viewport = viewport;
	}
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setViewportAsync(const float4& viewport, int32 taskIndex)
{
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not asynchronous.");
	GARDEN_ASSERT(taskIndex < 0 || taskIndex < Vulkan::secondaryCommandBuffers.size());
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	int32 taskCount;
	if (taskIndex < 0)
	{
		taskIndex = 0;
		taskCount = (uint32)Vulkan::secondaryCommandBuffers.size();
	}
	else
	{
		taskCount = taskIndex + 1;
	}

	vk::Viewport vkViewport(viewport.x, viewport.y, viewport.z, viewport.w, 0.0f, 1.0f); // TODO: support custom depth range.

	if (viewport == float4(0.0f))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		auto framebufferSize = framebufferView->getSize();
		vkViewport.width = framebufferSize.x;
		vkViewport.height = framebufferSize.y;
	}

	while (taskIndex < taskCount)
		Vulkan::secondaryCommandBuffers[taskIndex++].setViewport(0, 1, &vkViewport);
}

//**********************************************************************************************************************
void GraphicsPipeline::setScissor(const int4& scissor)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	SetScissorCommand command;
	if (scissor == int4(0))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		command.scissor = int4(int2(0), framebufferView->getSize());
	}
	else
	{
		command.scissor = scissor;
	}
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setScissorAsync(const int4& scissor, int32 taskIndex)
{
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	
	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not asynchronous.");
	GARDEN_ASSERT(taskIndex < 0 || taskIndex < Vulkan::secondaryCommandBuffers.size());
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	int32 taskCount;
	if (taskIndex < 0)
	{
		taskIndex = 0;
		taskCount = (uint32)Vulkan::secondaryCommandBuffers.size();
	}
	else
	{
		taskCount = taskIndex + 1;
	}

	vk::Rect2D vkScissor({ scissor.x, scissor.y }, { (uint32)scissor.z, (uint32)scissor.w });

	if (scissor == int4(0))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		auto framebufferSize = framebufferView->getSize();
		vkScissor.extent.width = (uint32)framebufferSize.x;
		vkScissor.extent.height = (uint32)framebufferSize.y;
	}

	while (taskIndex < taskCount)
		Vulkan::secondaryCommandBuffers[taskIndex++].setScissor(0, 1, &vkScissor);
}

//**********************************************************************************************************************
void GraphicsPipeline::setViewportScissor(const float4& viewportScissor)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	SetViewportScissorCommand command;
	if (viewportScissor == float4(0.0f))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		command.viewportScissor = float4(float2(0.0f), framebufferView->getSize());
	}
	else
	{
		command.viewportScissor = viewportScissor;
	}
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setViewportScissorAsync(const float4& viewportScissor, int32 taskIndex)
{
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	
	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not asynchronous.");
	GARDEN_ASSERT(taskIndex < 0 || taskIndex < Vulkan::secondaryCommandBuffers.size());
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	int32 taskCount;
	if (taskIndex < 0)
	{
		taskIndex = 0;
		taskCount = (uint32)Vulkan::secondaryCommandBuffers.size();
	}
	else
	{
		taskCount = taskIndex + 1;
	}

	vk::Viewport vkViewport(viewportScissor.x, viewportScissor.y,
		viewportScissor.z, viewportScissor.w, 0.0f, 1.0f); // TODO: support custom depth range.
	vk::Rect2D vkScissor({ (int32)viewportScissor.x, (int32)viewportScissor.y },
		{ (uint32)viewportScissor.z, (uint32)viewportScissor.w });

	if (viewportScissor == float4(0.0f))
	{
		auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
		auto framebufferSize = framebufferView->getSize();
		vkViewport.width = framebufferSize.x;
		vkViewport.height = framebufferSize.y;
		vkScissor.extent.width = (uint32)framebufferSize.x;
		vkScissor.extent.height = (uint32)framebufferSize.y;
	}

	while (taskIndex < taskCount)
	{
		auto secondaryCommandBuffer = Vulkan::secondaryCommandBuffers[taskIndex++];
		secondaryCommandBuffer.setViewport(0, 1, &vkViewport);
		secondaryCommandBuffer.setScissor(0, 1, &vkScissor);
	}
}

//**********************************************************************************************************************
void GraphicsPipeline::draw(ID<Buffer> vertexBuffer, uint32 vertexCount,
	uint32 instanceCount, uint32 vertexOffset, uint32 instanceOffset) // TODO: support multiple buffer binding.
{
	GARDEN_ASSERT(vertexCount > 0);
	GARDEN_ASSERT(instanceCount > 0);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	DrawCommand command;
	command.vertexCount = vertexCount;
	command.instanceCount = instanceCount;
	command.vertexOffset = vertexOffset;
	command.instanceOffset = instanceOffset;
	command.vertexBuffer = vertexBuffer;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (vertexBuffer)
	{
		auto vertexBufferView = GraphicsAPI::bufferPool.get(vertexBuffer);
		GARDEN_ASSERT(vertexBufferView->instance); // is ready
		if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
		{
			vertexBufferView->readyLock++;
			GraphicsAPI::currentCommandBuffer->addLockResource(vertexBuffer);
		}
	}
}

//**********************************************************************************************************************
void GraphicsPipeline::drawAsync(int32 taskIndex, ID<Buffer> vertexBuffer,
	uint32 vertexCount, uint32 instanceCount, uint32 vertexOffset, uint32 instanceOffset)
{
	GARDEN_ASSERT(vertexCount > 0);
	GARDEN_ASSERT(instanceCount > 0);
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(taskIndex >= 0);
	GARDEN_ASSERT(taskIndex < Vulkan::secondaryCommandBuffers.size());
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not async.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	auto secondaryCommandBuffer = Vulkan::secondaryCommandBuffers[taskIndex];
	View<Buffer> vertexBufferView;

	if (vertexBuffer != Framebuffer::currentVertexBuffers[taskIndex])
	{
		vk::Buffer instance;
		if (vertexBuffer)
		{
			vertexBufferView = GraphicsAPI::bufferPool.get(vertexBuffer);
			GARDEN_ASSERT(vertexBufferView->instance); // is ready
			instance = (VkBuffer)vertexBufferView->instance;
		}
		else
		{
			instance = nullptr;
		}
		
		const vk::DeviceSize size = 0;
		secondaryCommandBuffer.bindVertexBuffers(0, 1, (vk::Buffer*)&instance, &size);
		Framebuffer::currentVertexBuffers[taskIndex] = vertexBuffer;
	}

	secondaryCommandBuffer.draw(vertexCount, instanceCount, vertexOffset, instanceOffset);
	Vulkan::secondaryCommandStates[taskIndex] = true;

	DrawCommand command;
	command.asyncRecording = true;
	command.vertexBuffer = vertexBuffer;

	GraphicsAPI::currentCommandBuffer->commandMutex.lock();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	if (vertexBuffer)
	{
		if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
		{
			vertexBufferView->readyLock++;
			GraphicsAPI::currentCommandBuffer->addLockResource(vertexBuffer);
		}
	}
	GraphicsAPI::currentCommandBuffer->commandMutex.unlock();
}

//**********************************************************************************************************************
void GraphicsPipeline::drawIndexed(ID<Buffer> vertexBuffer, ID<Buffer> indexBuffer, Index indexType,
	uint32 indexCount, uint32 instanceCount, uint32 indexOffset, uint32 instanceOffset, uint32 vertexOffset)
{
	GARDEN_ASSERT(vertexBuffer);
	GARDEN_ASSERT(indexBuffer);
	GARDEN_ASSERT(indexCount > 0);
	GARDEN_ASSERT(instanceCount > 0);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	auto vertexBufferView = GraphicsAPI::bufferPool.get(vertexBuffer);
	auto indexBufferView = GraphicsAPI::bufferPool.get(indexBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is asynchronous.");
	GARDEN_ASSERT(vertexBufferView->instance); // is ready
	GARDEN_ASSERT(indexBufferView->instance); // is ready
	GARDEN_ASSERT(indexCount + indexOffset <= indexBufferView->getBinarySize() / toBinarySize(indexType));
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	DrawIndexedCommand command;
	command.indexType = indexType;
	command.indexCount = indexCount;
	command.instanceCount = instanceCount;
	command.indexOffset = indexOffset;
	command.instanceOffset = instanceOffset;
	command.vertexOffset = vertexOffset;
	command.vertexBuffer = vertexBuffer;
	command.indexBuffer = indexBuffer;
	GraphicsAPI::currentCommandBuffer->addCommand(command);

	if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
	{
		vertexBufferView->readyLock++;
		indexBufferView->readyLock++;
		GraphicsAPI::currentCommandBuffer->addLockResource(vertexBuffer);
		GraphicsAPI::currentCommandBuffer->addLockResource(indexBuffer);
	}
}

//**********************************************************************************************************************
void GraphicsPipeline::drawIndexedAsync(int32 taskIndex, ID<Buffer> vertexBuffer,
	ID<Buffer> indexBuffer, Index indexType, uint32 indexCount, uint32 instanceCount,
	uint32 indexOffset, uint32 instanceOffset, uint32 vertexOffset)
{
	GARDEN_ASSERT(vertexBuffer);
	GARDEN_ASSERT(indexBuffer);
	GARDEN_ASSERT(indexCount > 0);
	GARDEN_ASSERT(instanceCount > 0);
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(taskIndex >= 0);
	GARDEN_ASSERT(taskIndex < Vulkan::secondaryCommandBuffers.size());
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	auto vertexBufferView = GraphicsAPI::bufferPool.get(vertexBuffer);
	auto indexBufferView = GraphicsAPI::bufferPool.get(indexBuffer);
	
	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not async.");
	GARDEN_ASSERT(vertexBufferView->instance); // is ready
	GARDEN_ASSERT(indexBufferView->instance); // is ready
	GARDEN_ASSERT(indexCount + indexOffset <= indexBufferView->getBinarySize() / toBinarySize(indexType));
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	auto secondaryCommandBuffer = Vulkan::secondaryCommandBuffers[taskIndex];
	const vk::DeviceSize size = 0;

	if (vertexBuffer != Framebuffer::currentVertexBuffers[taskIndex])
	{
		secondaryCommandBuffer.bindVertexBuffers(0, 1, (vk::Buffer*)&vertexBufferView->instance, &size);
		Framebuffer::currentVertexBuffers[taskIndex] = vertexBuffer;
	}
	if (indexBuffer != Framebuffer::currentIndexBuffers[taskIndex])
	{
		secondaryCommandBuffer.bindIndexBuffer((VkBuffer)indexBufferView->instance,
			(vk::DeviceSize)(indexOffset * toBinarySize(indexType)), toVkIndexType(indexType));
		Framebuffer::currentIndexBuffers[taskIndex] = indexBuffer;
	}

	secondaryCommandBuffer.drawIndexed(indexCount, instanceCount, indexOffset, vertexOffset, instanceOffset);
	Vulkan::secondaryCommandStates[taskIndex] = true;

	DrawIndexedCommand command;
	command.asyncRecording = true;
	command.vertexBuffer = vertexBuffer;
	command.indexBuffer = indexBuffer;

	GraphicsAPI::currentCommandBuffer->commandMutex.lock();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	if (GraphicsAPI::currentCommandBuffer != &GraphicsAPI::frameCommandBuffer)
	{
		vertexBufferView->readyLock++;
		indexBufferView->readyLock++;
		GraphicsAPI::currentCommandBuffer->addLockResource(vertexBuffer);
		GraphicsAPI::currentCommandBuffer->addLockResource(indexBuffer);
	}
	GraphicsAPI::currentCommandBuffer->commandMutex.unlock();
}

//**********************************************************************************************************************
void GraphicsPipeline::drawFullscreen()
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (!Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is async.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif
	
	DrawCommand command;
	command.vertexCount = 3;
	command.instanceCount = 1;
	GraphicsAPI::currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::drawFullscreenAsync(int32 taskIndex)
{
	GARDEN_ASSERT(instance); // is ready
	GARDEN_ASSERT(asyncRecording);
	GARDEN_ASSERT(taskIndex >= 0);
	GARDEN_ASSERT(taskIndex < Vulkan::secondaryCommandBuffers.size());
	GARDEN_ASSERT(Framebuffer::getCurrent());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);

	#if GARDEN_DEBUG
	if (Vulkan::secondaryCommandBuffers.empty())
		throw runtime_error("Current render pass is not async.");
	auto framebufferView = GraphicsAPI::framebufferPool.get(Framebuffer::getCurrent());
	GARDEN_ASSERT(!framebufferView->renderPass || framebuffer == Framebuffer::getCurrent());
	GARDEN_ASSERT(subpassIndex == Framebuffer::getCurrentSubpassIndex());
	#endif

	auto secondaryCommandBuffer = Vulkan::secondaryCommandBuffers[taskIndex];
	secondaryCommandBuffer.draw(3, 1, 0, 0);
	Vulkan::secondaryCommandStates[taskIndex] = true;
}
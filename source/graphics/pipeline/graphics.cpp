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

#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/graphics/vulkan/api.hpp"

using namespace garden;
using namespace garden::graphics;

//**********************************************************************************************************************
static vk::PrimitiveTopology toVkPrimitiveTopology(GraphicsPipeline::Topology topology) noexcept
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
static vk::PolygonMode toVkPolygonMode(GraphicsPipeline::Polygon polygon) noexcept
{
	switch (polygon)
	{
	case GraphicsPipeline::Polygon::Fill: return vk::PolygonMode::eFill;
	case GraphicsPipeline::Polygon::Line: return vk::PolygonMode::eLine;
	case GraphicsPipeline::Polygon::Point: return vk::PolygonMode::ePoint;
	default: abort();
	}
}
static vk::CullModeFlags toVkCullMode(GraphicsPipeline::CullFace cullFace) noexcept
{
	switch (cullFace)
	{
	case GraphicsPipeline::CullFace::Front: return vk::CullModeFlagBits::eFront;
	case GraphicsPipeline::CullFace::Back: return vk::CullModeFlagBits::eBack;
	case GraphicsPipeline::CullFace::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
	default: abort();
	}
}
static vk::FrontFace toVkFrontFace(GraphicsPipeline::FrontFace frontFace) noexcept
{
	switch (frontFace)
	{
	case GraphicsPipeline::FrontFace::Clockwise: return vk::FrontFace::eClockwise;
	case GraphicsPipeline::FrontFace::CounterClockwise: return vk::FrontFace::eCounterClockwise;
	default: abort();
	}
}

//**********************************************************************************************************************
static vk::BlendFactor toVkBlendFactor(GraphicsPipeline::BlendFactor blendFactor) noexcept
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
static vk::BlendOp toVkBlendOp(GraphicsPipeline::BlendOperation blendOperation) noexcept
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
static constexpr vk::ColorComponentFlags toVkColorComponents(GraphicsPipeline::ColorComponent colorComponents) noexcept
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
void GraphicsPipeline::createVkInstance(GraphicsCreateData& createData)
{
	if (variantCount > 1)
		this->instance = malloc<vk::Pipeline>(variantCount);

	constexpr uint8 maxStageCount = 2;
	ShaderStage shaderStages[maxStageCount]; 
	vector<uint8> codeArray[maxStageCount];
	uint8 stageCount = 0;

	if (!createData.vertexCode.empty())
	{
		shaderStages[stageCount] = ShaderStage::Vertex;
		codeArray[stageCount] = std::move(createData.vertexCode);
		stageCount++;
	}
	if (!createData.fragmentCode.empty())
	{
		shaderStages[stageCount] = ShaderStage::Fragment;
		codeArray[stageCount] = std::move(createData.fragmentCode);
		stageCount++;
	}

	auto shaders = createShaders(codeArray, stageCount, createData.shaderPath);
	vk::PipelineShaderStageCreateInfo stageInfos[maxStageCount];
	vk::SpecializationInfo specializationInfos[maxStageCount];

	for (uint8 i = 0; i < stageCount; i++)
	{
		auto shaderStage = shaderStages[i]; auto specializationInfo = &specializationInfos[i];
		fillVkSpecConsts(createData.shaderPath, specializationInfo, 
			createData.specConsts, createData.specConstValues, shaderStage, variantCount);
		vk::PipelineShaderStageCreateInfo stageInfo({}, toVkShaderStage(shaderStage), (VkShaderModule)shaders[i], 
			"main", specializationInfo->mapEntryCount > 0 ? specializationInfo : nullptr);
		stageInfos[i] = stageInfo;
	}

	vk::VertexInputBindingDescription bindingDescription(0, 
		createData.vertexAttributesSize, vk::VertexInputRate::eVertex);
	vk::PipelineVertexInputStateCreateInfo inputInfo;
	vector<vk::VertexInputAttributeDescription> inputAttributes(createData.vertexAttributes.size());

	if (!createData.vertexAttributes.empty())
	{
		const auto& vertexAttributes = createData.vertexAttributes;
		for (uint32 i = 0; i < (uint32)inputAttributes.size(); i++)
		{
			auto attribute = vertexAttributes[i];
			vk::VertexInputAttributeDescription inputAttribute(i, 0, 
				toVkFormat(attribute.type, attribute.format), attribute.offset);
			inputAttributes[i] = inputAttribute;
		}

		inputInfo.vertexBindingDescriptionCount = 1;
		inputInfo.pVertexBindingDescriptions = &bindingDescription;
		inputInfo.vertexAttributeDescriptionCount = (uint32)inputAttributes.size();
		inputInfo.pVertexAttributeDescriptions = inputAttributes.data();
		// TODO: allow to specify input rate for an each vertex attribute?
	}

	vk::PipelineViewportStateCreateInfo viewportInfo({}, 1, nullptr, 1, nullptr); // TODO: pass it as argument.

	vk::PipelineMultisampleStateCreateInfo multisampleInfo({},
		vk::SampleCountFlagBits::e1, VK_FALSE, 1.0f, nullptr, VK_FALSE, VK_FALSE);

	vk::GraphicsPipelineCreateInfo pipelineInfo({}, stageCount, stageInfos, &inputInfo, 
		nullptr, nullptr, &viewportInfo, nullptr, &multisampleInfo, nullptr, nullptr, nullptr,
		(VkPipelineLayout)pipelineLayout, nullptr, createData.subpassIndex, nullptr, -1);

	vk::PipelineRenderingCreateInfoKHR dynamicRenderingInfo;
	vector<vk::Format> dynamicColorFormats;

	GARDEN_ASSERT_MSG(createData.blendStates.size() == createData.colorFormats.size(), 
		"Different shader output [" + to_string(createData.blendStates.size()) + "] and "
		"framebuffer attachment count [" + to_string(createData.colorFormats.size()) +  "] "
		"in graphics pipeline [" + createData.shaderPath.generic_string() +"]");

	if (!createData.renderPass)
	{
		const auto& colorFormats = createData.colorFormats;
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

	// TODO: pass dynamic states as argument.
	// Also allow to specify static viewport and scissor.
	vector<vk::DynamicState> dynamicStates;
	dynamicStates.push_back(vk::DynamicState::eViewport);
	dynamicStates.push_back(vk::DynamicState::eScissor);
	dynamicStates.push_back(vk::DynamicState::eDepthBias);
	vk::PipelineDynamicStateCreateInfo dynamicInfo;

	if (!dynamicStates.empty())
	{
		dynamicInfo.dynamicStateCount = (uint32)dynamicStates.size();
		dynamicInfo.pDynamicStates = dynamicStates.data();
		pipelineInfo.pDynamicState = &dynamicInfo;
	}

	auto vulkanAPI = VulkanAPI::get();
	const auto& pipelineStateOverrides = createData.pipelineStateOverrides;
	const auto& blendStateOverrides = createData.blendStateOverrides;

	for (uint8 i = 0; i < variantCount; i++)
	{
		if (variantCount > 1)
		{
			for (auto& specializationInfo : specializationInfos)
				setVkVariantIndex(&specializationInfo, i);
		}

		auto pipelineStateSearch = pipelineStateOverrides.find(i);
		const auto& pipelineState = pipelineStateSearch == pipelineStateOverrides.end() ?
			createData.pipelineState : pipelineStateSearch->second;

		#if GARDEN_DEBUG
		if (pipelineState.depthTesting || pipelineState.depthWriting || pipelineState.stencilTesting)
		{
			GARDEN_ASSERT_MSG(createData.depthStencilFormat != Image::Format::Undefined, 
				"No depth/stencil buffer in framebuffer for graphics pipeline [" + 
				createData.shaderPath.generic_string() + "]");
		}
		#endif

		vk::PipelineInputAssemblyStateCreateInfo assemblyInfo({},
			toVkPrimitiveTopology(pipelineState.topology), VK_FALSE); // TODO: support primitive restarting

		vk::PipelineRasterizationStateCreateInfo rasterizationInfo({},
			pipelineState.depthClamping, pipelineState.discarding, toVkPolygonMode(pipelineState.polygon),
			pipelineState.faceCulling ? toVkCullMode(pipelineState.cullFace) : vk::CullModeFlagBits::eNone,
			toVkFrontFace(pipelineState.frontFace), pipelineState.depthBiasing, pipelineState.depthBiasConstant,
			pipelineState.depthBiasClamp, pipelineState.depthBiasSlope, 1.0f);

		vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
		if (createData.depthStencilFormat != Image::Format::Undefined)
		{
			depthStencilInfo.depthTestEnable = pipelineState.depthTesting;
			depthStencilInfo.depthWriteEnable = pipelineState.depthWriting;
			depthStencilInfo.depthCompareOp = toVkCompareOp(pipelineState.depthCompare);
			depthStencilInfo.stencilTestEnable = pipelineState.stencilTesting;
			depthStencilInfo.minDepthBounds = 0.0f;
			depthStencilInfo.maxDepthBounds = 1.0f;
			// TODO: stencil testing, depth boundsTesting
		}

		auto blendStateSearch = blendStateOverrides.find(i);
		const auto& blendStates = blendStateSearch == blendStateOverrides.end() ?
			createData.blendStates : blendStateSearch->second;
		vector<vk::PipelineColorBlendAttachmentState> blendAttachments(attachmentCount);

		for (uint8 j = 0; j < attachmentCount; j++)
		{
			auto blendState = blendStates.at(j);
			vk::PipelineColorBlendAttachmentState blendAttachment(blendState.blending,
				toVkBlendFactor(blendState.srcColorFactor), toVkBlendFactor(blendState.dstColorFactor),
				toVkBlendOp(blendState.colorOperation), toVkBlendFactor(blendState.srcAlphaFactor),
				toVkBlendFactor(blendState.dstAlphaFactor), toVkBlendOp(blendState.alphaOperation),
				toVkColorComponents(blendState.colorMask));
			blendAttachments[j] = blendAttachment;
		}

		array<float, 4> blendConstant =
		{
			pipelineState.blendConstant.x, pipelineState.blendConstant.y,
			pipelineState.blendConstant.z, pipelineState.blendConstant.w
		};

		vk::PipelineColorBlendStateCreateInfo blendInfo({}, VK_FALSE, {},
			(uint32)blendAttachments.size(), blendAttachments.data(), blendConstant); // TODO: logical operations

		pipelineInfo.pInputAssemblyState = &assemblyInfo;
		pipelineInfo.pRasterizationState = &rasterizationInfo;
		pipelineInfo.pDepthStencilState = &depthStencilInfo;
		pipelineInfo.pColorBlendState = &blendInfo;

		auto result = vulkanAPI->device.createGraphicsPipeline(vulkanAPI->pipelineCache, pipelineInfo);
		vk::detail::resultCheck(result.result, "vk::Device::createGraphicsPipeline");

		if (variantCount > 1)
			((void**)this->instance)[i] = result.value;
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
GraphicsPipeline::GraphicsPipeline(GraphicsCreateData& createData, 
	bool asyncRecording) : Pipeline(createData, asyncRecording)
{
	this->attachmentCount = (uint8)createData.blendStates.size();
	
	if (GraphicsAPI::get()->getBackendType() == GraphicsBackend::VulkanAPI)
		createVkInstance(createData);
	else abort();
}

static void checkFramebufferSubpass(GraphicsAPI* graphicsAPI, ID<Framebuffer> framebuffer, uint8 subpassIndex)
{
	GARDEN_ASSERT(framebuffer == graphicsAPI->currentFramebuffer);
	GARDEN_ASSERT(subpassIndex == graphicsAPI->currentSubpassIndex);
}

//**********************************************************************************************************************
void GraphicsPipeline::updateFramebuffer(ID<Framebuffer> framebuffer)
{
	GARDEN_ASSERT_MSG(framebuffer, "Assert " + debugName);
	#if GARDEN_DEBUG
	auto framebufferView = GraphicsAPI::get()->framebufferPool.get(framebuffer);
	GARDEN_ASSERT_MSG(framebufferView->getSubpasses().empty(), "Assert " + debugName);
	GARDEN_ASSERT_MSG(attachmentCount == framebufferView->getColorAttachments().size(), "Different graphics pipeline "
		"[" + debugName + "] and framebuffer [" + framebufferView->getDebugName() + "] attachment count");
	#endif
	this->framebuffer = framebuffer;
}

void GraphicsPipeline::setViewport(float4 viewport)
{
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");
	auto graphicsAPI = GraphicsAPI::get();

	// TODO: support multiple viewport/scissor count. MacBook intel viewport max count is 16.
	SetViewportCommand command;
	if (viewport == float4::zero)
	{
		auto framebufferView = graphicsAPI->framebufferPool.get(graphicsAPI->currentFramebuffer);
		command.viewport = float4(float2::zero, framebufferView->getSize());
	}
	else
	{
		command.viewport = viewport;
	}
	graphicsAPI->currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setViewportAsync(float4 viewport, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		vk::Viewport vkViewport(viewport.x, viewport.y, viewport.z, viewport.w, 0.0f, 1.0f); // TODO: support custom depth range.

		if (viewport == float4::zero)
		{
			auto framebufferView = vulkanAPI->framebufferPool.get(vulkanAPI->currentFramebuffer);
			auto framebufferSize = framebufferView->getSize();
			vkViewport.width = framebufferSize.x;
			vkViewport.height = framebufferSize.y;
		}

		while (threadIndex < autoThreadCount)
			vulkanAPI->secondaryCommandBuffers[threadIndex++].setViewport(0, 1, &vkViewport);
	}
	else abort();
}

//**********************************************************************************************************************
void GraphicsPipeline::setScissor(int4 scissor)
{
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");
	auto graphicsAPI = GraphicsAPI::get();

	SetScissorCommand command;
	if (scissor == int4::zero)
	{
		auto framebufferView = graphicsAPI->framebufferPool.get(graphicsAPI->currentFramebuffer);
		command.scissor = int4(int2::zero, framebufferView->getSize());
	}
	else
	{
		command.scissor = scissor;
	}
	graphicsAPI->currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setScissorAsync(int4 scissor, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		vk::Rect2D vkScissor({ scissor.x, scissor.y }, { (uint32)scissor.z, (uint32)scissor.w });

		if (scissor == int4::zero)
		{
			auto framebufferView = vulkanAPI->framebufferPool.get(vulkanAPI->currentFramebuffer);
			auto framebufferSize = framebufferView->getSize();
			vkScissor.extent.width = (uint32)framebufferSize.x;
			vkScissor.extent.height = (uint32)framebufferSize.y;
		}

		while (threadIndex < autoThreadCount)
			vulkanAPI->secondaryCommandBuffers[threadIndex++].setScissor(0, 1, &vkScissor);
	}
	else abort();
}

//**********************************************************************************************************************
void GraphicsPipeline::setViewportScissor(float4 viewportScissor)
{
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");
	auto graphicsAPI = GraphicsAPI::get();

	SetViewportScissorCommand command;
	if (viewportScissor == float4::zero)
	{
		auto framebufferView = graphicsAPI->framebufferPool.get(GraphicsAPI::get()->currentFramebuffer);
		command.viewportScissor = float4(float2::zero, framebufferView->getSize());
	}
	else
	{
		command.viewportScissor = viewportScissor;
	}
	graphicsAPI->currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setViewportScissorAsync(float4 viewportScissor, int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		vk::Viewport vkViewport(viewportScissor.x, viewportScissor.y,
			viewportScissor.z, viewportScissor.w, 0.0f, 1.0f); // TODO: support custom depth range.
		vk::Rect2D vkScissor({ (int32)viewportScissor.x, (int32)viewportScissor.y },
			{ (uint32)viewportScissor.zero, (uint32)viewportScissor.w });

		if (viewportScissor == float4::zero)
		{
			auto framebufferView = vulkanAPI->framebufferPool.get(vulkanAPI->currentFramebuffer);
			auto framebufferSize = framebufferView->getSize();
			vkViewport.width = framebufferSize.x;
			vkViewport.height = framebufferSize.y;
			vkScissor.extent.width = (uint32)framebufferSize.x;
			vkScissor.extent.height = (uint32)framebufferSize.y;
		}

		while (threadIndex < autoThreadCount)
		{
			auto secondaryCommandBuffer = vulkanAPI->secondaryCommandBuffers[threadIndex++];
			secondaryCommandBuffer.setViewport(0, 1, &vkViewport);
			secondaryCommandBuffer.setScissor(0, 1, &vkScissor);
		}
	}
	else abort();
}

//**********************************************************************************************************************
void GraphicsPipeline::draw(ID<Buffer> vertexBuffer, uint32 vertexCount,
	uint32 instanceCount, uint32 vertexOffset, uint32 instanceOffset) // TODO: support multiple buffer binding.
{
	GARDEN_ASSERT_MSG(vertexCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instanceCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);

	DrawCommand command;
	command.vertexCount = vertexCount;
	command.instanceCount = instanceCount;
	command.vertexOffset = vertexOffset;
	command.instanceOffset = instanceOffset;
	command.vertexBuffer = vertexBuffer;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (vertexBuffer)
	{
		auto vertexBufferView = graphicsAPI->bufferPool.get(vertexBuffer);
		GARDEN_ASSERT_MSG(ResourceExt::getInstance(**vertexBufferView), "Vertex buffer [" + 
			vertexBufferView->getDebugName() + "] is not ready");
		if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			ResourceExt::getBusyLock(**vertexBufferView)++;
			graphicsAPI->currentCommandBuffer->addLockedResource(vertexBuffer);
		}
	}
}

//**********************************************************************************************************************
void GraphicsPipeline::drawAsync(int32 threadIndex, ID<Buffer> vertexBuffer,
	uint32 vertexCount, uint32 instanceCount, uint32 vertexOffset, uint32 instanceOffset)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(vertexCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instanceCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex >= 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);

	View<Buffer> vertexBufferView;
	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto secondaryCommandBuffer = vulkanAPI->secondaryCommandBuffers[threadIndex];

		if (vertexBuffer != vulkanAPI->currentVertexBuffers[threadIndex])
		{
			vk::Buffer instance;
			if (vertexBuffer)
			{
				vertexBufferView = vulkanAPI->bufferPool.get(vertexBuffer);
				GARDEN_ASSERT_MSG(ResourceExt::getInstance(**vertexBufferView), "Vertex buffer [" + 
					vertexBufferView->getDebugName() + "] is not ready");
				instance = (VkBuffer)ResourceExt::getInstance(**vertexBufferView);
			}
			else
			{
				instance = nullptr;
			}

			const vk::DeviceSize size = 0;
			secondaryCommandBuffer.bindVertexBuffers(0, 1, (vk::Buffer*)&instance, &size);
			vulkanAPI->currentVertexBuffers[threadIndex] = vertexBuffer;
		}

		secondaryCommandBuffer.draw(vertexCount, instanceCount, vertexOffset, instanceOffset);
		vulkanAPI->secondaryCommandStates[threadIndex]->store(true);
	}
	else abort();

	DrawCommand command;
	command.asyncRecording = true;
	command.vertexBuffer = vertexBuffer;

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	currentCommandBuffer->commandMutex.lock();
	currentCommandBuffer->addCommand(command);
	if (vertexBuffer)
	{
		if (currentCommandBuffer != graphicsAPI->frameCommandBuffer)
		{
			ResourceExt::getBusyLock(**vertexBufferView)++;
			currentCommandBuffer->addLockedResource(vertexBuffer);
		}
	}
	currentCommandBuffer->commandMutex.unlock();
}

//**********************************************************************************************************************
void GraphicsPipeline::drawIndexed(ID<Buffer> vertexBuffer, ID<Buffer> indexBuffer, IndexType indexType,
	uint32 indexCount, uint32 instanceCount, uint32 indexOffset, uint32 vertexOffset, uint32 instanceOffset)
{
	GARDEN_ASSERT_MSG(vertexBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(indexBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(indexCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instanceCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto vertexBufferView = graphicsAPI->bufferPool.get(vertexBuffer);
	auto indexBufferView = graphicsAPI->bufferPool.get(indexBuffer);
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**vertexBufferView), "Vertex buffer [" + 
		vertexBufferView->getDebugName() + "] is not ready");
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**indexBufferView), "Index buffer [" + 
		indexBufferView->getDebugName() + "] is not ready");
	GARDEN_ASSERT(indexCount + indexOffset <= indexBufferView->getBinarySize() / toBinarySize(indexType));
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);

	DrawIndexedCommand command;
	command.indexType = indexType;
	command.indexCount = indexCount;
	command.instanceCount = instanceCount;
	command.indexOffset = indexOffset;
	command.vertexOffset = vertexOffset;
	command.instanceOffset = instanceOffset;
	command.vertexBuffer = vertexBuffer;
	command.indexBuffer = indexBuffer;
	graphicsAPI->currentCommandBuffer->addCommand(command);

	if (graphicsAPI->currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		ResourceExt::getBusyLock(**vertexBufferView)++;
		ResourceExt::getBusyLock(**indexBufferView)++;
		graphicsAPI->currentCommandBuffer->addLockedResource(vertexBuffer);
		graphicsAPI->currentCommandBuffer->addLockedResource(indexBuffer);
	}
}

//**********************************************************************************************************************
void GraphicsPipeline::drawIndexedAsync(int32 threadIndex, ID<Buffer> vertexBuffer,
	ID<Buffer> indexBuffer, IndexType indexType, uint32 indexCount, uint32 instanceCount,
	uint32 indexOffset, uint32 vertexOffset, uint32 instanceOffset)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(vertexBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(indexBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(indexCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instanceCount > 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex >= 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	auto vertexBufferView = graphicsAPI->bufferPool.get(vertexBuffer);
	auto indexBufferView = graphicsAPI->bufferPool.get(indexBuffer);
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**vertexBufferView), "Vertex buffer [" + 
		vertexBufferView->getDebugName() + "] is not ready");
	GARDEN_ASSERT_MSG(ResourceExt::getInstance(**indexBufferView), "Index buffer [" + 
		indexBufferView->getDebugName() + "] is not ready");
	GARDEN_ASSERT(indexCount + indexOffset <= indexBufferView->getBinarySize() / toBinarySize(indexType));
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto secondaryCommandBuffer = vulkanAPI->secondaryCommandBuffers[threadIndex];
		const vk::DeviceSize size = 0;

		if (vertexBuffer != vulkanAPI->currentVertexBuffers[threadIndex])
		{
			vk::Buffer vkBuffer = (VkBuffer)ResourceExt::getInstance(**vertexBufferView);
			secondaryCommandBuffer.bindVertexBuffers(0, 1, &vkBuffer, &size);
			vulkanAPI->currentVertexBuffers[threadIndex] = vertexBuffer;
		}
		if (indexBuffer != vulkanAPI->currentIndexBuffers[threadIndex])
		{
			secondaryCommandBuffer.bindIndexBuffer((VkBuffer)ResourceExt::getInstance(**indexBufferView),
				(vk::DeviceSize)(indexOffset * toBinarySize(indexType)), toVkIndexType(indexType));
			vulkanAPI->currentIndexBuffers[threadIndex] = indexBuffer;
		}

		secondaryCommandBuffer.drawIndexed(indexCount, instanceCount, indexOffset, vertexOffset, instanceOffset);
		vulkanAPI->secondaryCommandStates[threadIndex]->store(true);
	}
	else abort();

	DrawIndexedCommand command;
	command.asyncRecording = true;
	command.vertexBuffer = vertexBuffer;
	command.indexBuffer = indexBuffer;

	auto currentCommandBuffer = graphicsAPI->currentCommandBuffer;
	currentCommandBuffer->commandMutex.lock();
	currentCommandBuffer->addCommand(command);
	if (currentCommandBuffer != graphicsAPI->frameCommandBuffer)
	{
		ResourceExt::getBusyLock(**vertexBufferView)++;
		ResourceExt::getBusyLock(**indexBufferView)++;
		currentCommandBuffer->addLockedResource(vertexBuffer);
		currentCommandBuffer->addLockedResource(indexBuffer);
	}
	currentCommandBuffer->commandMutex.unlock();
}

//**********************************************************************************************************************
void GraphicsPipeline::drawFullscreen()
{
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(!GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);
	
	DrawCommand command;
	command.vertexCount = 3;
	command.instanceCount = 1;
	graphicsAPI->currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::drawFullscreenAsync(int32 threadIndex)
{
	GARDEN_ASSERT_MSG(asyncRecording, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex >= 0, "Assert " + debugName);
	GARDEN_ASSERT_MSG(threadIndex < GraphicsAPI::get()->threadCount, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentFramebuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->currentCommandBuffer, "Assert " + debugName);
	GARDEN_ASSERT_MSG(GraphicsAPI::get()->isCurrentRenderPassAsync, "Assert " + debugName);
	GARDEN_ASSERT_MSG(instance, "Graphics pipeline [" + debugName + "] is not ready");

	auto graphicsAPI = GraphicsAPI::get();
	checkFramebufferSubpass(graphicsAPI, framebuffer, subpassIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		auto secondaryCommandBuffer = vulkanAPI->secondaryCommandBuffers[threadIndex];
		secondaryCommandBuffer.draw(3, 1, 0, 0);
		vulkanAPI->secondaryCommandStates[threadIndex]->store(true);
	}
	else abort();
}

void GraphicsPipeline::setDepthBias(float constantFactor, float slopeFactor, float clamp)
{
	GARDEN_ASSERT(GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(!GraphicsAPI::get()->isCurrentRenderPassAsync);
	
	SetDepthBiasCommand command;
	command.constantFactor = constantFactor;
	command.slopeFactor = slopeFactor;
	command.clamp = clamp;
	GraphicsAPI::get()->currentCommandBuffer->addCommand(command);
}
void GraphicsPipeline::setDepthBiasAsync(float constantFactor, float slopeFactor, float clamp, int32 threadIndex)
{
	GARDEN_ASSERT(threadIndex < GraphicsAPI::get()->threadCount);
	GARDEN_ASSERT(GraphicsAPI::get()->currentFramebuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->currentCommandBuffer);
	GARDEN_ASSERT(GraphicsAPI::get()->isCurrentRenderPassAsync);

	auto graphicsAPI = GraphicsAPI::get();
	auto autoThreadCount = graphicsAPI->calcAutoThreadCount(threadIndex);

	if (graphicsAPI->getBackendType() == GraphicsBackend::VulkanAPI)
	{
		auto vulkanAPI = VulkanAPI::get();
		while (threadIndex < autoThreadCount)
			vulkanAPI->secondaryCommandBuffers[threadIndex++].setDepthBias(constantFactor, clamp, slopeFactor);
	}
	else abort();
}
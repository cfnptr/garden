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

#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

static ID<ComputePipeline> createDownsampleNorm()
{
	ResourceSystem::ComputeOptions options;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadComputePipeline("process/downsample-norm", options);
}
static ID<ComputePipeline> createDownsampleNormA()
{
	ResourceSystem::ComputeOptions options;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadComputePipeline("process/downsample-norm-a", options);
}
static ID<GraphicsPipeline> createBoxBlur()
{
	ResourceSystem::GraphicsOptions options;
	options.loadAsync = false;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/box-blur",
		GraphicsSystem::Instance::get()->getSwapchainFramebuffer(), options);
}
static ID<GraphicsPipeline> createBilateralBlurD()
{
	ResourceSystem::GraphicsOptions options;
	options.loadAsync = false;

	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/bilateral-blur-d",
		GraphicsSystem::Instance::get()->getSwapchainFramebuffer(), options);
}

//**********************************************************************************************************************
GpuProcessSystem::GpuProcessSystem(bool setSingleton)
{
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", GpuProcessSystem::deinit);
}
GpuProcessSystem::~GpuProcessSystem()
{
	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", GpuProcessSystem::deinit);
	unsetSingleton();
}

void GpuProcessSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(bilatBlurDPipeline);
		graphicsSystem->destroy(boxBlurPipeline);
	}
}

ID<ComputePipeline> GpuProcessSystem::getDownsampleNorm()
{
	if (!downsampleNormPipeline)
		downsampleNormPipeline = createDownsampleNorm();
	return downsampleNormPipeline;
}
ID<ComputePipeline> GpuProcessSystem::getDownsampleNormA()
{
	if (!downsampleNormAPipeline)
		downsampleNormAPipeline = createDownsampleNormA();
	return downsampleNormAPipeline;
}
ID<GraphicsPipeline> GpuProcessSystem::getBoxBlur()
{
	if (!boxBlurPipeline)
		boxBlurPipeline = createBoxBlur();
	return boxBlurPipeline;
}
ID<GraphicsPipeline> GpuProcessSystem::getBilateralBlurD()
{
	if (!bilatBlurDPipeline)
		bilatBlurDPipeline = createBilateralBlurD();
	return bilatBlurDPipeline;
}

//**********************************************************************************************************************
void GpuProcessSystem::generateMips(ID<Image> image, ID<ComputePipeline> pipeline)
{
	GARDEN_ASSERT(image);
	GARDEN_ASSERT(pipeline);
	GARDEN_ASSERT(GraphicsSystem::Instance::get()->isRecording());

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto baseImageView = graphicsSystem->get(image);
	auto mipCount = baseImageView->getMipCount();
	GARDEN_ASSERT_MSG(mipCount > 1, "Image have only one mip level");

	auto imageType = baseImageView->getType();
	auto layerCount = baseImageView->getLayerCount();
	vector<ID<ImageView>> imageViews(mipCount);
	auto imageSize = max((uint2)baseImageView->getSize() / 2, uint2(1));
	auto pipelineView = graphicsSystem->get(pipeline);
	pipelineView->bind();

	imageViews[0] = graphicsSystem->createImageView(
		image, imageType, Image::Format::Undefined, 0, 1);
	SET_RESOURCE_DEBUG_NAME(imageViews[0], "imageView.normalMap.mip0");

	for (uint8 i = 1; i < mipCount; i++)
	{
		imageViews[i] = graphicsSystem->createImageView(
			image, imageType, Image::Format::Undefined, i, 1);
		SET_RESOURCE_DEBUG_NAME(imageViews[0], "imageView.normalMap.mip" + to_string(i));

		DescriptorSet::Uniforms uniforms
		{
			{ "srcBuffer", DescriptorSet::Uniform(imageViews[i - 1]) },
			{ "dstBuffer", DescriptorSet::Uniform(imageViews[i]) }
		};
		auto descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.normalMapMips" + to_string(i));

		pipelineView->bindDescriptorSet(descriptorSet);
		pipelineView->dispatch(uint3(imageSize, layerCount));

		graphicsSystem->destroy(descriptorSet);
		imageSize = max(imageSize / 2, uint2(1));
	}

	for (auto imageView : imageViews)
		graphicsSystem->destroy(imageView);
}
void GpuProcessSystem::normalMapMips(ID<Image> normalMap)
{
	GARDEN_ASSERT(normalMap);
	GARDEN_ASSERT(GraphicsSystem::Instance::get()->isRecording());

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto normalMapView = graphicsSystem->get(normalMap);

	ID<ComputePipeline> pipeline;
	if (normalMapView->getLayerCount() > 1)
	{
		if (!downsampleNormAPipeline)
			downsampleNormAPipeline = createDownsampleNormA();
		pipeline = downsampleNormAPipeline;
	}
	else
	{
		if (!downsampleNormPipeline)
			downsampleNormPipeline = createDownsampleNorm();
		pipeline = downsampleNormPipeline;
	}

	generateMips(normalMap, pipeline);
}

//**********************************************************************************************************************
void GpuProcessSystem::bilateralBlurD(ID<ImageView> srcBuffer, ID<Framebuffer> dstFramebuffer, ID<ImageView> tmpBuffer, 
	ID<Framebuffer> tmpFramebuffer, float2 scale, float sharpness, ID<DescriptorSet>& descriptorSet)
{
	GARDEN_ASSERT(srcBuffer);
	GARDEN_ASSERT(dstFramebuffer);
	GARDEN_ASSERT(tmpBuffer);
	GARDEN_ASSERT(tmpFramebuffer);
	GARDEN_ASSERT(GraphicsSystem::Instance::get()->isRecording());

	if (!bilatBlurDPipeline)
		bilatBlurDPipeline = createBilateralBlurD();

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!descriptorSet)
	{
		auto hizBuffer = HizRenderSystem::Instance::get()->getImageViews()[0];
		DescriptorSet::Uniforms uniforms
		{
			{ "srcBuffer", DescriptorSet::Uniform({ { srcBuffer }, { tmpBuffer } }) },
			{ "hizBuffer", DescriptorSet::Uniform(hizBuffer, 1, 2) }
		};
		descriptorSet = graphicsSystem->createDescriptorSet(bilatBlurDPipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.bilateralBlurD" + to_string(*descriptorSet));
	}

	auto& cameraConstants = graphicsSystem->getCameraConstants();

	BilateralBlurPC pc;
	pc.nearPlane = cameraConstants.nearPlane;
	pc.sharpness = sharpness;

	auto pipelineView = graphicsSystem->get(bilatBlurDPipeline);
	auto framebufferView = graphicsSystem->get(tmpFramebuffer);
	auto texelSize = scale / framebufferView->getSize();
	pipelineView->updateFramebuffer(tmpFramebuffer);
	pc.texelSize = float2(texelSize.x, 0.0f);

	SET_GPU_DEBUG_LABEL("Bilateral Blur (D)", Color::transparent);

	framebufferView->beginRenderPass(float4::zero);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, 0);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
	framebufferView->endRenderPass();

	framebufferView = graphicsSystem->get(dstFramebuffer);
	pipelineView->updateFramebuffer(dstFramebuffer);
	pc.texelSize = float2(0.0f, texelSize.y);

	framebufferView->beginRenderPass(float4::zero);
	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->bindDescriptorSet(descriptorSet, 1);
	pipelineView->pushConstants(&pc);
	pipelineView->drawFullscreen();
	framebufferView->endRenderPass();

	pipelineView->updateFramebuffer(graphicsSystem->getSwapchainFramebuffer());
}
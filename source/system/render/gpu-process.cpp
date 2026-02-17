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

#include "garden/system/render/gpu-process.hpp"
#include "garden/system/render/hiz.hpp"
#include "garden/system/resource.hpp"
#include "process/gaussian-blur.h"
#include "math/brdf.hpp"

using namespace garden;

static ID<Buffer> createGgxKernel(GraphicsSystem* graphicsSystem)
{
	float2 coeffs[brdf::ggxCoeffCount];
	GpuProcessSystem::calcGaussCoeffs(brdf::ggxSigma0, coeffs, brdf::ggxCoeffCount);

	auto kernel = graphicsSystem->createBuffer(Buffer::Usage::Storage | Buffer::Usage::TransferDst | 
		Buffer::Usage::TransferQ, Buffer::CpuAccess::None, coeffs, brdf::ggxCoeffCount * 
		sizeof(float2), Buffer::Location::PreferGPU, Buffer::Strategy::Size);
	SET_RESOURCE_DEBUG_NAME(kernel, "buffer.storage.ggxBlurKernel");
	return kernel;
}

//**********************************************************************************************************************
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
static ID<GraphicsPipeline> createBoxBlur(ID<Framebuffer> framebuffer)
{
	ResourceSystem::GraphicsOptions options;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/box-blur", framebuffer, options);
}
static ID<GraphicsPipeline> createBilateralBlurD(ID<Framebuffer> framebuffer, uint32 kernelRadius)
{
	Pipeline::SpecConstValues specConsts = { { "KERNEL_RADIUS", Pipeline::SpecConstValue(kernelRadius) }, };

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConsts;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/bilateral-blur-d", framebuffer, options);
}
static ID<GraphicsPipeline> createGaussianBlur(ID<Framebuffer> framebuffer, uint32 coeffCount)
{
	Pipeline::SpecConstValues specConsts = { { "COEFF_COUNT", Pipeline::SpecConstValue(coeffCount) }, };

	ResourceSystem::GraphicsOptions options;
	options.specConstValues = &specConsts;
	options.loadAsync = false;
	return ResourceSystem::Instance::get()->loadGraphicsPipeline("process/gaussian-blur", framebuffer, options);
}

//**********************************************************************************************************************
GpuProcessSystem::GpuProcessSystem(bool setSingleton)
{
	auto manager = Manager::Instance::get();
	ECSM_SUBSCRIBE_TO_EVENT("Deinit", GpuProcessSystem::deinit);
}
GpuProcessSystem::~GpuProcessSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		ECSM_UNSUBSCRIBE_FROM_EVENT("Deinit", GpuProcessSystem::deinit);
	}
	unsetSingleton();
}

void GpuProcessSystem::deinit()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto graphicsSystem = GraphicsSystem::Instance::get();
		graphicsSystem->destroy(downsampleNormAPipeline);
		graphicsSystem->destroy(downsampleNormPipeline);
		graphicsSystem->destroy(ggxBlurKernel);
	}
}

ID<Buffer> GpuProcessSystem::getGgxBlurKernel()
{
	if (!ggxBlurKernel)
		ggxBlurKernel = createGgxKernel(GraphicsSystem::Instance::get());
	return ggxBlurKernel;
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
	auto imageSize = max((uint2)baseImageView->getSize() / 2, uint2(1));
	auto pipelineView = graphicsSystem->get(pipeline);
	pipelineView->bind();

	vector<ID<ImageView>> imageViews(mipCount);
	imageViews[0] = graphicsSystem->createImageView(
		image, imageType, Image::Format::Undefined, 0, 0, 0, 1);
	SET_RESOURCE_DEBUG_NAME(imageViews[0], "imageView.normalMap.mip0");

	for (uint8 i = 1; i < mipCount; i++)
	{
		imageViews[i] = graphicsSystem->createImageView(
			image, imageType, Image::Format::Undefined, 0, 0, i, 1);
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

	graphicsSystem->destroy(imageViews);
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
void GpuProcessSystem::calcGaussCoeffs(float sigma, float2* coeffs, uint8 coeffCount) noexcept
{
	GARDEN_ASSERT(sigma > 0.0f);
	GARDEN_ASSERT(coeffs);
	GARDEN_ASSERT(coeffCount > 0);
	
	auto alpha = 1.0f / (-2.0f * sigma * sigma);
	auto totalWeight = 1.0f;
	coeffs[0] = float2(1.0f, 0.0f);

	for (uint8 i = 1; i < coeffCount; i++)
	{
		auto x0 = float(i * 2 - 1), x1 = float(i * 2);
		auto k0 = std::exp(alpha * x0 * x0);
		auto k1 = std::exp(alpha * x1 * x1);
		auto k = k0 + k1, o = k1 / k;
		coeffs[i] = float2(k, o);
		totalWeight += (k0 + k1) * 2.0f;
	}

	for (uint8 i = 0; i < coeffCount; i++)
		coeffs[i].x /= totalWeight;
}

void GpuProcessSystem::gaussianBlur(ID<ImageView> srcBuffer, ID<Framebuffer> dstFramebuffer,
	ID<Framebuffer> tmpFramebuffer, ID<Buffer> kernelBuffer, float intensity, bool reinhard, 
	ID<GraphicsPipeline>& pipeline, ID<DescriptorSet>& descriptorSet)
{
	GARDEN_ASSERT(srcBuffer);
	GARDEN_ASSERT(dstFramebuffer);
	GARDEN_ASSERT(tmpFramebuffer);
	GARDEN_ASSERT(kernelBuffer);
	GARDEN_ASSERT(GraphicsSystem::Instance::get()->isRecording());

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!pipeline)
	{
		auto kenrelBufferView = graphicsSystem->get(kernelBuffer);
		GARDEN_ASSERT(kenrelBufferView->getBinarySize() % sizeof(float2) == 0);
		auto coeffCount = kenrelBufferView->getBinarySize() / sizeof(float2);
		pipeline = createGaussianBlur(dstFramebuffer, coeffCount);
	}

	if (!descriptorSet)
	{
		auto tmpFramebufferView = graphicsSystem->get(tmpFramebuffer);
		auto tmpBufferView = tmpFramebufferView->getColorAttachments()[0].imageView;
		DescriptorSet::Uniforms uniforms 
		{
			{ "srcBuffer", DescriptorSet::Uniform({ { srcBuffer }, { tmpBufferView } })},
			{ "kernel", DescriptorSet::Uniform(kernelBuffer, 1, 2) }
		};
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.gaussianBlur" + to_string(*descriptorSet));
	}

	auto variant = reinhard ? GAUSSIAN_BLUR_REINHARD : GAUSSIAN_BLUR_BASE;
	auto texelSize = float2(1.0f) / graphicsSystem->get(srcBuffer)->calcSize();
	auto pipelineView = graphicsSystem->get(pipeline);

	GaussianBlurPC pc;
	pc.intensity = intensity;

	SET_GPU_DEBUG_LABEL("Gaussian Blur");
	pipelineView->updateFramebuffer(tmpFramebuffer);
	pc.texelSize = float2(texelSize.x, 0.0f);
	{
		RenderPass renderPass(tmpFramebuffer, float4::zero);
		pipelineView->bind(variant);
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, 0);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}

	pipelineView->updateFramebuffer(dstFramebuffer);
	pc.texelSize = float2(0.0f, texelSize.y);
	{
		RenderPass renderPass(dstFramebuffer, float4::zero);
		pipelineView->bind(variant);
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, 1);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void GpuProcessSystem::bilateralBlurD(ID<ImageView> srcBuffer, ID<ImageView> depthBuffer,
	ID<Framebuffer> dstFramebuffer, ID<Framebuffer> tmpFramebuffer, float sharpness, ID<GraphicsPipeline>& pipeline, 
	ID<DescriptorSet>& descriptorSet, uint8 kernelRadius, bool useDstSize)
{
	GARDEN_ASSERT(srcBuffer);
	GARDEN_ASSERT(depthBuffer);
	GARDEN_ASSERT(dstFramebuffer);
	GARDEN_ASSERT(tmpFramebuffer);
	GARDEN_ASSERT(sharpness > 0.0f);
	GARDEN_ASSERT(kernelRadius > 0);
	GARDEN_ASSERT(GraphicsSystem::Instance::get()->isRecording());

	if (!pipeline)
		pipeline = createBilateralBlurD(dstFramebuffer, kernelRadius);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	if (!descriptorSet)
	{
		auto tmpFramebufferView = graphicsSystem->get(tmpFramebuffer);
		auto tmpBufferView = tmpFramebufferView->getColorAttachments()[0].imageView;
		DescriptorSet::Uniforms uniforms
		{
			{ "srcBuffer", DescriptorSet::Uniform({ { srcBuffer }, { tmpBufferView } }) },
			{ "depthBuffer", DescriptorSet::Uniform(depthBuffer, 1, 2) }
		};
		descriptorSet = graphicsSystem->createDescriptorSet(pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.bilateralBlurD" + to_string(*descriptorSet));
	}

	auto& cc = graphicsSystem->getCommonConstants();
	auto texelSize = float2(1.0f) / graphicsSystem->get(useDstSize ? depthBuffer : srcBuffer)->calcSize();

	BilateralBlurPC pc;
	pc.nearPlane = cc.nearPlane;
	pc.sharpness = -sharpness;

	auto pipelineView = graphicsSystem->get(pipeline);
	pipelineView->updateFramebuffer(tmpFramebuffer);
	pc.texelSize = float2(texelSize.x, 0.0f);

	SET_GPU_DEBUG_LABEL("Bilateral Blur (D)");
	{
		RenderPass renderPass(tmpFramebuffer, float4::zero);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, 0);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}

	pipelineView->updateFramebuffer(dstFramebuffer);
	pc.texelSize = float2(0.0f, texelSize.y);
	{
		RenderPass renderPass(dstFramebuffer, float4::zero);
		pipelineView->bind();
		pipelineView->setViewportScissor();
		pipelineView->bindDescriptorSet(descriptorSet, 1);
		pipelineView->pushConstants(&pc);
		pipelineView->drawFullscreen();
	}
}

//**********************************************************************************************************************
void GpuProcessSystem::prepareGgxBlur(ID<Image> buffer, vector<ID<Framebuffer>>& framebuffers)
{
	GARDEN_ASSERT(buffer);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bufferView = graphicsSystem->get(buffer);
	auto roughnessLodCount = bufferView->getMipCount();
	auto framebufferSize = (uint2)bufferView->getSize();
	GARDEN_ASSERT(bufferView->getLayerCount() >= 2);
	GARDEN_ASSERT(bufferView->getMipCount() > 1);

	if (framebuffers.size() < roughnessLodCount * 2)
		framebuffers.resize(roughnessLodCount * 2);
	auto framebufferData = framebuffers.data();

	for (uint8 mip = 0; mip < roughnessLodCount; mip++)
	{
		for (uint8 layer = 0; layer < 2; layer++)
		{
			auto index = layer * roughnessLodCount + mip;
			auto framebuffer = framebufferData[index];
			auto colorView = bufferView->getView(layer, mip);

			if (framebuffer)
			{
				Framebuffer::OutputAttachment colorAttachment(colorView, { false, false, true });
				auto framebufferView = graphicsSystem->get(framebuffer);
				framebufferView->update(framebufferSize, &colorAttachment, 1);
			}
			else
			{
				vector<Framebuffer::OutputAttachment> colorAttachments =
				{ Framebuffer::OutputAttachment(colorView, { false, false, true }) };
				framebuffer = graphicsSystem->createFramebuffer(framebufferSize, std::move(colorAttachments));
				SET_RESOURCE_DEBUG_NAME(framebuffer, "framebuffer.ggxBlur" + to_string(index));
				framebufferData[index] = framebuffer;
			}
		}
		framebufferSize = max(framebufferSize / 2u, uint2::one);
	}
}
bool GpuProcessSystem::ggxBlur(ID<Image> buffer, const vector<ID<Framebuffer>>& framebuffers, 
	ID<GraphicsPipeline>& pipeline, vector<ID<DescriptorSet>>& descriptorSets)
{
	GARDEN_ASSERT(buffer);
	GARDEN_ASSERT(framebuffers.size() > 2);

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto bufferView = graphicsSystem->get(buffer);
	auto roughnessLodCount = bufferView->getMipCount();
	GARDEN_ASSERT(bufferView->getLayerCount() >= 2);
	GARDEN_ASSERT(bufferView->getMipCount() > 1);

	auto blurKernel = getGgxBlurKernel();
	if (!graphicsSystem->get(blurKernel)->isReady())
		return false;

	if (descriptorSets.size() < roughnessLodCount)
		descriptorSets.resize(roughnessLodCount);

	Image::BlitRegion blitRegion;
	blitRegion.layerCount = 1;

	auto framebufferData = framebuffers.data();
	auto reinhard = true;

	SET_GPU_DEBUG_LABEL("GGX Blur");
	
	for (uint8 mip = 1; mip < roughnessLodCount; mip++)
	{
		blitRegion.srcMipLevel = mip - 1;
		blitRegion.dstMipLevel = mip;
		Image::blit(buffer, buffer, blitRegion, Sampler::Filter::Linear);

		gaussianBlur(bufferView->getView(0, mip), framebufferData[mip], 
			framebufferData[roughnessLodCount + mip], ggxBlurKernel, 1.0f, 
			reinhard, pipeline, descriptorSets[mip]);
		reinhard = false;
	}
	return true;
}

const uint8 GpuProcessSystem::ggxCoeffCount = brdf::ggxCoeffCount;
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

#include "garden/system/render/model.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/resource.hpp"
#include "math/matrix/float.hpp"
#include "math/matrix/transform.hpp"
#include <filesystem>

using namespace garden;

// TODO: add bindless support

//**********************************************************************************************************************
ModelRenderSystem::ModelRenderSystem(const fs::path& pipelinePath, bool useNormalMapping, bool useGBuffer) : 
	useNormalMapping(useNormalMapping), useGBuffer(useGBuffer), pipelinePath(pipelinePath) { }

void ModelRenderSystem::init()
{
	InstanceRenderSystem::init();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", ModelRenderSystem::imageLoaded);

	#if GARDEN_DEBUG
	debugResourceName = pipelinePath.generic_string();
	#endif
}
void ModelRenderSystem::deinit()
{
	// TODO: somehow destroy default image view, maybe check it ref count?

	if (Manager::Instance::get()->isRunning)
		ECSM_UNSUBSCRIBE_FROM_EVENT("ImageLoaded", ModelRenderSystem::imageLoaded);

	InstanceRenderSystem::deinit();
}

//**********************************************************************************************************************
void ModelRenderSystem::imageLoaded()
{
	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto resourceSystem = ResourceSystem::Instance::get();
	auto image = resourceSystem->getLoadedImage();
	auto& imagePath = resourceSystem->getLoadedImagePaths()[0];
	auto& modelRenderPool = getMeshComponentPool();
	auto componentSize = getMeshComponentSize();
	auto componentData = (uint8*)modelRenderPool.getData();
	auto componentOccupancy = modelRenderPool.getOccupancy();
	Ref<DescriptorSet> descriptorSet = {};
	
	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto modelRenderView = (ModelRenderComponent*)(componentData + i * componentSize);
		if (modelRenderView->descriptorSet || modelRenderView->colorMap != image || 
			modelRenderView->mraorMap != image)
		{
			continue;
		}
		if (!descriptorSet && graphicsSystem->get(modelRenderView->colorMap)->isLoaded() &&
			graphicsSystem->get(modelRenderView->mraorMap)->isLoaded())
		{
			descriptorSet = createSharedDS(imagePath.generic_string(), 
				ID<Image>(modelRenderView->colorMap), ID<Image>(modelRenderView->mraorMap));
		}
		modelRenderView->descriptorSet = descriptorSet;
	}

	auto& modelFramePool = getAnimationFramePool();
	componentSize = getAnimationFrameSize();
	componentData = (uint8*)modelFramePool.getData();
	componentOccupancy = modelFramePool.getOccupancy();

	for (uint32 i = 0; i < componentOccupancy; i++)
	{
		auto modelRenderFrame = (ModelAnimationFrame*)(componentData + i * componentSize);
		if (modelRenderFrame->descriptorSet || modelRenderFrame->colorMap != image || 
			modelRenderFrame->mraorMap != image)
		{
			continue;
		}
		if (!descriptorSet)
		{
			descriptorSet = createSharedDS(imagePath.generic_string(), 
				ID<Image>(modelRenderFrame->colorMap), ID<Image>(modelRenderFrame->mraorMap));
		}
		modelRenderFrame->descriptorSet = descriptorSet;
	}
}

//**********************************************************************************************************************
void ModelRenderSystem::copyComponent(View<Component> source, View<Component> destination)
{
	auto destinationView = View<ModelRenderComponent>(destination);
	const auto sourceView = View<ModelRenderComponent>(source);

	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(destinationView->descriptorSet);
	resourceSystem->destroyShared(destinationView->mraorMap);
	resourceSystem->destroyShared(destinationView->colorMap);
	resourceSystem->destroyShared(destinationView->lodBuffer);

	destinationView->aabb = sourceView->aabb;
	destinationView->isEnabled = sourceView->isEnabled;
	destinationView->indexType = sourceView->indexType;
	destinationView->lodBuffer = sourceView->lodBuffer;
	destinationView->colorMap = sourceView->colorMap;
	destinationView->mraorMap = sourceView->mraorMap;
	destinationView->descriptorSet = sourceView->descriptorSet;
	destinationView->indexCount = sourceView->indexCount;

	#if GARDEN_DEBUG || GARDEN_EDITOR
	destinationView->lodBufferPaths = sourceView->lodBufferPaths;
	destinationView->colorMapPath = sourceView->colorMapPath;
	destinationView->mraorMapPath = sourceView->mraorMapPath;
	#endif
}

//**********************************************************************************************************************
void ModelRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto modelRenderView = (ModelRenderComponent*)meshRenderView;
	if (!modelRenderView->descriptorSet || !modelRenderView->lodBuffer)
		return;

	ID<Buffer> vertexBuffer, indexBuffer;
	auto distanceSq = length3(getTranslation(model));
	auto lodBufferView = ResourceSystem::Instance::get()->get(modelRenderView->lodBuffer);
	if (!lodBufferView->getLevel(distanceSq, vertexBuffer, indexBuffer))
		return;

	DescriptorSet::Range dsRange[2];
	dsRange[0] = DescriptorSet::Range(descriptorSet, 1, swapchainIndex);
	dsRange[1] = DescriptorSet::Range((ID<DescriptorSet>)modelRenderView->descriptorSet);

	auto instanceData = (BaseInstanceData*)(instanceMap + drawIndex * getBaseInstanceDataSize());
	setInstanceData(modelRenderView, instanceData, viewProj, model, drawIndex, taskIndex);

	auto pushConstants = (PushConstants*)pipelineView->getPushConstants(taskIndex);
	setPushConstants(modelRenderView, pushConstants, viewProj, model, drawIndex, taskIndex);

	pipelineView->bindDescriptorSetsAsync(dsRange, 2, taskIndex);
	pipelineView->pushConstantsAsync(taskIndex);

	pipelineView->drawIndexedAsync(taskIndex, vertexBuffer, indexBuffer, 
		modelRenderView->indexType, modelRenderView->indexCount);
}

uint64 ModelRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(BaseInstanceData);
}
void ModelRenderSystem::setInstanceData(ModelRenderComponent* modelRenderView, BaseInstanceData* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	instanceData->mvp = (float4x4)(viewProj * model);
	instanceData->model = useNormalMapping ? (float4x4)model : (float4x4)transpose3x3(inverse4x4(model));
}
void ModelRenderSystem::setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 drawIndex, int32 taskIndex)
{
	pushConstants->instanceIndex = drawIndex;
}

//**********************************************************************************************************************
map<string, DescriptorSet::Uniform> ModelRenderSystem::getModelUniforms(ID<ImageView> colorMap, ID<ImageView> mraorMap)
{
	map<string, DescriptorSet::Uniform> modelUniforms =
	{
		{ "colorMap", DescriptorSet::Uniform(colorMap) },
		{ "mraorMap", DescriptorSet::Uniform(colorMap) }
	};
	return modelUniforms;
}
ID<GraphicsPipeline> ModelRenderSystem::createBasePipeline()
{
	ID<Framebuffer> framebuffer;
	auto deferredSystem = DeferredRenderSystem::Instance::tryGet();
	if (deferredSystem)
	{
		framebuffer = useGBuffer ? deferredSystem->getGFramebuffer() :
			deferredSystem->getMetaHdrFramebuffer();
	}
	else
	{
		framebuffer = ForwardRenderSystem::Instance::get()->getFramebuffer();
	}
	return ResourceSystem::Instance::get()->loadGraphicsPipeline(pipelinePath, framebuffer, true);
}

//**********************************************************************************************************************
static const vector<BufferChannel> fullModelChannels = 
{
	BufferChannel::Positions, BufferChannel::Normals, BufferChannel::TextureCoords, BufferChannel::Tangents
};
static const vector<BufferChannel> liteModelChannels = 
{
	BufferChannel::Positions, BufferChannel::Normals, BufferChannel::TextureCoords
};

void ModelRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto modelRenderView = View<ModelRenderComponent>(component);
	if (modelRenderView->aabb != Aabb::one)
		serializer.write("aabb", modelRenderView->aabb);
	if (!modelRenderView->isEnabled)
		serializer.write("isEnabled", false);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!modelRenderView->lodBufferPaths.empty())
	{
		auto& lodBufferPaths = modelRenderView->lodBufferPaths;
		auto arraySize = (uint32)lodBufferPaths.size();

		serializer.beginChild("lodBufferPaths");
		for (uint32 i = 0; i < arraySize; i++)
		{
			serializer.beginArrayElement();
			serializer.write(lodBufferPaths[i].generic_string());
			serializer.endArrayElement();
		}
		serializer.endChild();
	}
	
	if (!modelRenderView->colorMapPath.empty())
		serializer.write("colorMapPath", modelRenderView->colorMapPath.generic_string());
	if (!modelRenderView->mraorMapPath.empty())
		serializer.write("mraorMapPath", modelRenderView->mraorMapPath.generic_string());
	#endif
}
void ModelRenderSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto modelRenderView = View<ModelRenderComponent>(component);
	deserializer.read("aabb", modelRenderView->aabb);
	deserializer.read("isEnabled", modelRenderView->isEnabled);

	constexpr auto bind = Image::Bind::TransferDst | Image::Bind::Sampled;
	constexpr auto strategy = Image::Strategy::Default;
	constexpr auto flags = ImageLoadFlags::LoadShared;
	auto resourceSystem = ResourceSystem::Instance::get();

	string resourcePath;
	if (deserializer.beginChild("lodBufferPaths"))
	{
		auto arraySize = (uint32)deserializer.getArraySize();
		vector<fs::path> lodBufferPaths(arraySize);

		for (uint32 i = 0; i < arraySize; i++)
		{
			if (!deserializer.beginArrayElement(i))
				break;

			deserializer.read(resourcePath);
			deserializer.endArrayElement();

			if (resourcePath.empty())
			{
				lodBufferPaths.resize(i);
				break;
			}

			lodBufferPaths[i] = resourcePath;
		}
		deserializer.endChild();

		if (lodBufferPaths.empty())
			lodBufferPaths.push_back("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		modelRenderView->lodBufferPaths = lodBufferPaths;
		#endif
		modelRenderView->lodBuffer = resourceSystem->loadLodBuffer(lodBufferPaths, 
			useNormalMapping ? fullModelChannels : liteModelChannels);

		if (deserializer.beginChild("lodBufferSplits"))
		{
			arraySize = min(arraySize, (uint32)deserializer.getArraySize());
			auto lodBufferView = resourceSystem->get(modelRenderView->lodBuffer);

			for (uint32 i = 0; i < arraySize; i++)
			{
				if (!deserializer.beginArrayElement(i))
					break;

				float splitSq = 0.0f;
				deserializer.read(splitSq);
				deserializer.endArrayElement();

				if (splitSq >= 0.0f)
					lodBufferView->setSplit(i, splitSq);
			}
			deserializer.endChild();
		}
	}

	deserializer.read("colorMapPath", resourcePath);
	if (resourcePath.empty())
		resourcePath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	modelRenderView->colorMapPath = resourcePath;
	#endif
	modelRenderView->colorMap = resourceSystem->loadImage(resourcePath, bind, 1, strategy, flags);

	deserializer.read("mraorMapPath", resourcePath);
	if (resourcePath.empty())
		resourcePath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	modelRenderView->mraorMapPath = resourcePath;
	#endif
	modelRenderView->mraorMap = resourceSystem->loadImage(resourcePath, bind, 1, strategy, flags);
}

//**********************************************************************************************************************
void ModelRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	auto modelFrameView = View<ModelAnimationFrame>(frame);
	if (modelFrameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)modelFrameView->isEnabled);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (modelFrameView->animateLodBuffer)
	{
		if (!modelFrameView->lodBufferPath.empty())
			serializer.write("lodBufferPath", modelFrameView->lodBufferPath.generic_string());
	}
	if (modelFrameView->animateTextureMaps)
	{
		if (!modelFrameView->colorMapPath.empty())
			serializer.write("colorMapPath", modelFrameView->colorMapPath.generic_string());
		if (!modelFrameView->mraorMapPath.empty())
			serializer.write("mraorMapPath", modelFrameView->mraorMapPath.generic_string());
	}
	#endif
}
void ModelRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto modelRenderView = View<ModelRenderComponent>(component);
	auto frameA = View<ModelAnimationFrame>(a);
	auto frameB = View<ModelAnimationFrame>(b);

	if (frameA->animateIsEnabled)
		modelRenderView->isEnabled = (bool)round(t);

	if (frameA->animateTextureMaps)
	{
		if (round(t) > 0.0f)
		{
			if (frameB->descriptorSet)
			{
				modelRenderView->colorMap = frameB->colorMap;
				modelRenderView->mraorMap = frameB->mraorMap;
				modelRenderView->descriptorSet = frameB->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				modelRenderView->colorMapPath = frameB->colorMapPath;
				modelRenderView->mraorMapPath = frameB->mraorMapPath;
				#endif
			}
		}
		else
		{
			if (frameA->descriptorSet)
			{
				modelRenderView->colorMap = frameA->colorMap;
				modelRenderView->mraorMap = frameB->mraorMap;
				modelRenderView->descriptorSet = frameA->descriptorSet;
				#if GARDEN_DEBUG || GARDEN_EDITOR
				modelRenderView->colorMapPath = frameA->colorMapPath;
				modelRenderView->mraorMapPath = frameB->mraorMapPath;
				#endif
			}
		}
	}
}
void ModelRenderSystem::deserializeAnimation(IDeserializer& deserializer, ModelAnimationFrame& frame)
{
	auto boolValue = true;
	frame.animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frame.isEnabled = boolValue;

	constexpr auto bind = Image::Bind::TransferDst | Image::Bind::Sampled;
	constexpr auto strategy = Image::Strategy::Default;
	constexpr auto flags = ImageLoadFlags::LoadShared;
	auto resourceSystem = ResourceSystem::Instance::get();

	string resourcePath;
	frame.animateLodBuffer = deserializer.read("lodBufferPath", resourcePath);
	if (resourcePath.empty())
		resourcePath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.lodBufferPath = resourcePath;
	#endif
	frame.lodBuffer = {}; // TODO:

	frame.animateTextureMaps = deserializer.read("colorMapPath", resourcePath);
	if (resourcePath.empty())
		resourcePath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.colorMapPath = resourcePath;
	#endif
	frame.colorMap = resourceSystem->loadImage(resourcePath, bind, 1, strategy, flags);

	frame.animateTextureMaps &= deserializer.read("mraorMapPath", resourcePath);
	if (resourcePath.empty())
		resourcePath = "missing";
	#if GARDEN_DEBUG || GARDEN_EDITOR
	frame.mraorMapPath = resourcePath;
	#endif
	frame.mraorMap = resourceSystem->loadImage(resourcePath, bind, 1, strategy, flags);

	frame.descriptorSet = {}; // See the imageLoaded()
}

//**********************************************************************************************************************
void ModelRenderSystem::destroyResources(View<ModelAnimationFrame> frameView)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(frameView->colorMap);
	resourceSystem->destroyShared(frameView->descriptorSet);
	frameView->colorMap = {};
	frameView->descriptorSet = {};
}
void ModelRenderSystem::destroyResources(View<ModelRenderComponent> modelRenderView)
{
	auto resourceSystem = ResourceSystem::Instance::get();
	resourceSystem->destroyShared(modelRenderView->lodBuffer);
	resourceSystem->destroyShared(modelRenderView->colorMap);
	resourceSystem->destroyShared(modelRenderView->mraorMap);
	resourceSystem->destroyShared(modelRenderView->descriptorSet);
	modelRenderView->lodBuffer = {};
	modelRenderView->colorMap = {};
	modelRenderView->mraorMap = {};
	modelRenderView->descriptorSet = {};
}

Ref<DescriptorSet> ModelRenderSystem::createSharedDS(const string& path, ID<Image> colorMap, ID<Image> mraorMap)
{
	GARDEN_ASSERT(!path.empty());
	GARDEN_ASSERT(colorMap);
	GARDEN_ASSERT(mraorMap);

	auto hashState = Hash128::getState();
	Hash128::resetState(hashState);
	Hash128::updateState(hashState, path.c_str(), path.length());

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto colorImageView = graphicsSystem->get(colorMap);
	auto imageSize = (uint2)colorImageView->getSize();
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));

	auto mraorImageView = graphicsSystem->get(mraorMap);
	imageSize = (uint2)mraorImageView->getSize();
	Hash128::updateState(hashState, &imageSize.x, sizeof(uint32));
	Hash128::updateState(hashState, &imageSize.y, sizeof(uint32));

	auto uniforms = getModelUniforms(colorImageView->getDefaultView(), mraorImageView->getDefaultView());
	auto descriptorSet = ResourceSystem::Instance::get()->createSharedDS(
		Hash128::digestState(hashState), getBasePipeline(), std::move(uniforms), 1);
	SET_RESOURCE_DEBUG_NAME(descriptorSet, "descriptorSet.shared." + path);
	return descriptorSet;
}
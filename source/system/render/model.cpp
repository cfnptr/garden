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

#include "garden/system/render/model.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/log.hpp"
#include "model/instance-data.h"

#include "assimp/Importer.hpp"
#include "assimp/DefaultLogger.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

using namespace garden;

namespace garden
{
	class ErrorLogStream : public Assimp::LogStream
	{
	public:
		void write(const char* message) override
		{
			GARDEN_LOG_ERROR(message);
		}
	};
};

void ModelRenderComponent::setLodCount(uint8 count)
{
	auto lodCount = getLodCount(), lodCapacity = getLodCapacity();
	if (count > lodCapacity)
	{
		if (lods) lods = realloc(lods, count);
		else lods = calloc<ModelLOD>(count);
		_setLodCapacity(count);
	}
	
	if (count > lodCount) // Note: assuming that ModelLOD can be zero initialized.
		memset((uint8*)lods + lodCount * sizeof(ModelLOD), 0, (count - lodCount) * sizeof(ModelLOD));
	_setLodCount(count);
}

void ModelRenderSystem::init()
{
	InstanceRenderSystem::init();

	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", ModelRenderSystem::imageLoaded);

	#if GARDEN_DEBUG
	Assimp::DefaultLogger::create("");
	Assimp::DefaultLogger::get()->attachStream(new ErrorLogStream, Assimp::Logger::Err);
	debugResourceName = pipelinePath.generic_string();
	#endif
}

void ModelRenderSystem::imageLoaded()
{
	// TODO: update bindless pool and model components.
}

void ModelRenderSystem::resetComponent(View<Component> component)
{
	auto resourceSystem = ResourceSystem::getInstance();
	auto componentView = View<ModelRenderComponent>(component);
	auto lods = componentView->getLods();
	auto lodCount = componentView->getLodCount();

	for (uint32 i = 0; i < lodCount; i++)
	{
		auto& lod = lods[i];
		resourceSystem->destroyShared(lod.vertexBuffer);
		resourceSystem->destroyShared(lod.indexBuffer);
	}
}

//**********************************************************************************************************************
uint32 ModelRenderSystem::getReadyMeshesAsync(MeshRenderComponent* meshRenderView, 
	const f32x4& cameraPosition, const Frustum& frustum, f32x4x4& model)
{
	if (isBehindFrustum(frustum, meshRenderView->aabb, model))
		return 0;

	auto modelRenderView = (ModelRenderComponent*)meshRenderView;
	if (modelRenderView->getLodCount() == 0 || (modelRenderView->_colorMapID() & 
		modelRenderView->_normalMapID() & modelRenderView->_ormMapID() == 0))
	{
		return 0;
	}

	// TODO: isParticle

	return 1;
}
void ModelRenderSystem::drawAsync(MeshRenderComponent* meshRenderView,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	auto modelRenderView = (ModelRenderComponent*)meshRenderView;
	auto instanceData = (BaseInstanceData*)(instanceMap + instanceIndex * getBaseInstanceDataSize());
	setInstanceData(modelRenderView, instanceData, viewProj, model, instanceIndex, taskIndex);

	PushConstants pc;
	setPushConstants(modelRenderView, &pc, viewProj, model, instanceIndex, taskIndex);

	pipelineView->bindDescriptorSetAsync(descriptorSet, inFlightIndex, taskIndex);
	pipelineView->pushConstantsAsync(&pc, taskIndex);
	abort(); // TODO: pipelineView->drawIndexedAsync(taskIndex, {}, 6);
}

uint64 ModelRenderSystem::getBaseInstanceDataSize()
{
	return (uint64)sizeof(BaseInstanceData);
}
void ModelRenderSystem::setInstanceData(ModelRenderComponent* modelRenderView, void* instanceData,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	auto currModel = (float3x4)transpose4x4(model);
	auto modelData = (BaseInstanceData*)instanceData;
	modelData->currModel = currModel;
	modelData->uvSize = modelRenderView->uvSize;
	modelData->uvOffset = modelRenderView->uvOffset;
	modelData->colorAdd = modelRenderView->colorAdd;
	modelData->colorMul = modelRenderView->colorMul;
	modelData->prevModel = modelRenderView->lastModel;
	modelData->ormAdd = modelRenderView->ormAdd;
	modelData->ormMul = modelRenderView->ormMul;
	modelData->_alignment0 = 0;
	modelData->_alignment1 = 0;
	modelRenderView->lastModel = currModel;
}
void ModelRenderSystem::setPushConstants(ModelRenderComponent* modelRenderView, PushConstants* pushConstants,
	const f32x4x4& viewProj, const f32x4x4& model, uint32 instanceIndex, int32 taskIndex)
{
	pushConstants->instanceIndex = instanceIndex;
}

//**********************************************************************************************************************
DescriptorSet::Uniforms ModelRenderSystem::getModelUniforms(ID<ImageView> colorMap)
{
	DescriptorSet::Uniforms spriteUniforms = { { "colorMap", DescriptorSet::Uniform(colorMap) } };
	return spriteUniforms;
}
ID<GraphicsPipeline> ModelRenderSystem::createBasePipeline()
{
	auto deferredSystem = DeferredRenderSystem::tryGetInstance();
	ID<Framebuffer> framebuffer; GraphicsPipeline::State pipelineState;
	if (deferredSystem)
	{
		if (getMeshRenderType() == MeshRenderType::UI)
		{
			framebuffer = deferredSystem->getUiFramebuffer();
		}
		else
		{
			framebuffer = deferredSystem->getDepthStencilHdrFB();
			pipelineState.depthTesting = pipelineState.depthWriting = true;
		}
	}
	else
	{
		framebuffer = ForwardRenderSystem::getInstance()->getColorFramebuffer();
	}
	GraphicsPipeline::PipelineStates pipelineStates = { { 0, pipelineState } };

	ResourceSystem::GraphicsLoadOptions options;
	options.loadAsync = false; // We can't load async due to imageLoaded() usage.
	options.pipelineStateOverrides = &pipelineStates;
	return ResourceSystem::getInstance()->loadGraphicsPipeline(pipelinePath, framebuffer, &options);
}

//**********************************************************************************************************************
void ModelRenderSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<ModelRenderComponent>(component);
	if (componentView->aabb != Aabb::one)
		serializer.write("aabb", componentView->aabb);
	if (!componentView->isEnabled)
		serializer.write("isEnabled", false);
	if (componentView->uvSize != half2::one)
		serializer.write("uvSize", componentView->uvSize);
	if (componentView->uvOffset != half2::zero)
		serializer.write("uvOffset", componentView->uvOffset);
	if (componentView->colorAdd != Color::transparent)
		serializer.write("colorAdd", componentView->colorAdd);
	if (componentView->colorMul != Color::white)
		serializer.write("colorMul", componentView->colorMul);

	#if GARDEN_DEBUG || GARDEN_EDITOR
	if (!componentView->colorMapPath.empty())
		serializer.write("colorMapPath", componentView->colorMapPath.generic_string());
	if (!componentView->normalMapPath.empty())
		serializer.write("normalMapPath", componentView->normalMapPath.generic_string());
	if (!componentView->ormMapPath.empty())
		serializer.write("ormMapPath", componentView->ormMapPath.generic_string());
	#endif
}
void ModelRenderSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<ModelRenderComponent>(component);
	deserializer.read("aabb", componentView->aabb);
	deserializer.read("isEnabled", componentView->isEnabled);
	deserializer.read("uvSize", componentView->uvSize);
	deserializer.read("uvOffset", componentView->uvOffset);
	deserializer.read("colorAdd", componentView->colorAdd);
	deserializer.read("colorMul", componentView->colorMul);

	if (deserializer.read("colorMapPath", valueStringCache))
	{
		if (valueStringCache.empty()) valueStringCache.assign("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		componentView->colorMapPath = valueStringCache;
		#endif
		componentView->colorMap = ResourceSystem::getInstance()->loadSharedImage(valueStringCache + "/c");
	}
	if (deserializer.read("normalMapPath", valueStringCache))
	{
		if (valueStringCache.empty()) valueStringCache.assign("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		componentView->normalMapPath = valueStringCache;
		#endif
		componentView->normalMap = ResourceSystem::getInstance()->loadSharedImage(valueStringCache + "/n");
	}
	if (deserializer.read("ormMapPath", valueStringCache))
	{
		if (valueStringCache.empty()) valueStringCache.assign("missing");
		#if GARDEN_DEBUG || GARDEN_EDITOR
		componentView->ormMapPath = valueStringCache;
		#endif
		componentView->ormMap = ResourceSystem::getInstance()->loadSharedImage(valueStringCache + "/orm");
	}
}

//**********************************************************************************************************************
void ModelRenderSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<ModelAnimFrame>(frame);
	if (frameView->animateIsEnabled)
		serializer.write("isEnabled", (bool)frameView->isEnabled);
	if (frameView->animateUvSize)
		serializer.write("uvSize", frameView->uvSize);
	if (frameView->animateUvOffset)
		serializer.write("uvOffset", frameView->uvOffset);
	if (frameView->animateColorAdd)
		serializer.write("colorAdd", frameView->colorAdd);
	if (frameView->animateColorMul)
		serializer.write("colorMul", frameView->colorMul);
	if (frameView->animateOrmAdd)
		serializer.write("ormAdd", frameView->ormAdd);
	if (frameView->animateOrmMul)
		serializer.write("ormMul", frameView->ormMul);
}
void ModelRenderSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<ModelAnimFrame>(frame); auto boolValue = true;
	frameView->animateIsEnabled = deserializer.read("isEnabled", boolValue);
	frameView->animateUvSize = deserializer.read("uvSize", frameView->uvSize);
	frameView->animateUvOffset = deserializer.read("uvOffset", frameView->uvOffset);
	frameView->animateColorAdd = deserializer.read("colorAdd", frameView->colorAdd);
	frameView->animateColorMul = deserializer.read("colorMul", frameView->colorMul);
	frameView->animateOrmAdd = deserializer.read("ormAdd", frameView->ormAdd);
	frameView->animateOrmMul = deserializer.read("ormMul", frameView->ormMul);
}
void ModelRenderSystem::animateAsync(View<Component> component,
	View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<ModelRenderComponent>(component);
	const auto frameA = View<ModelAnimFrame>(a);
	const auto frameB = View<ModelAnimFrame>(b);

	if (frameA->animateIsEnabled)
		componentView->isEnabled = (bool)round(t) ? frameB->isEnabled : frameA->isEnabled;
	if (frameA->animateUvSize)
		componentView->uvSize = (half2)lerp((float2)frameA->uvSize, (float2)frameB->uvSize, t);
	if (frameA->animateUvOffset)
		componentView->uvOffset = (half2)lerp((float2)frameA->uvOffset, (float2)frameB->uvOffset, t);
	if (frameA->animateColorAdd)
		componentView->colorAdd = lerp(frameA->colorAdd, frameB->colorAdd, t);
	if (frameA->animateColorMul)
		componentView->colorMul = lerp(frameA->colorMul, frameB->colorMul, t);
	if (frameA->animateOrmAdd)
		componentView->ormAdd = lerp(frameA->ormAdd, frameB->ormAdd, t);
	if (frameA->animateOrmMul)
		componentView->ormMul = lerp(frameA->ormMul, frameB->ormMul, t);
}

//**********************************************************************************************************************
ID<Entity> ModelRenderSystem::loadFileData(const void* data, psize dataSize, const map<string, type_index>* components)
{
	GARDEN_ASSERT(data);
	GARDEN_ASSERT(dataSize > 0);

	constexpr uint32 processFlags = aiProcess_CalcTangentSpace | aiProcess_Triangulate | 
		aiProcess_JoinIdenticalVertices | aiProcess_SortByPType;

	Assimp::Importer importer;
	auto scene = importer.ReadFileFromMemory(data, dataSize, processFlags);
	if (!scene)
	{
		throw GardenError("Invalid Assimp 3D model data. ("
			"error: " + string(importer.GetErrorString()) + ")");
	}

	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto meshes = scene->mMeshes; auto materials = scene->mMaterials;

	stack<aiNode*> nodes; nodes.push(scene->mRootNode);
	vector<ModelLOD> tmpLods; map<uint32, ModelLOD> sharedLods;
	ID<Entity> rootEntity = {}, lastEntity = {}; uint32 lodCount = 0;

	while (!nodes.empty())
	{
		auto node = nodes.top(); nodes.pop();
		auto newEntity = manager->createEntity();
		if (!rootEntity)
			rootEntity = newEntity;

		auto transformView = manager->add<TransformComponent>(newEntity);
		transformView->setParent(lastEntity);
		lastEntity = newEntity;

		auto model = *((const f32x4x4*)&node->mTransformation);
		transformView->setTransform(transpose4x4(model));
		#if GARDEN_DEBUG || GARDEN_EDITOR
		transformView->debugName = node->mName.C_Str();
		#endif

		auto modelRenderView = manager->add<ModelRenderComponent>(newEntity);
		auto& lods = modelRenderView->lods;
		auto childrenCount = node->mNumChildren;
		auto children = node->mChildren;
	
		for (uint32 i = 0; i < childrenCount; i++)
		{
			auto child = children[i];
			auto lodStr = strstr(child->mName.C_Str(), "_LOD");
			auto lodStrLength = lodStr ? strlen(lodStr) : 0;
	
			if (lodStrLength > 4)
			{
				auto meshIds = child->mMeshes;
				auto meshIdCount = child->mNumMeshes;

				for (uint32 j = 0; j < meshIdCount; j++)
				{
					auto meshId = meshIds[j];
					auto searchResult = sharedLods.find(meshId);
					if (searchResult != sharedLods.end())
					{
						
					}

					//auto mesh = meshes[];
					//mesh->mMethod
				}

				uint8 lodIndex = 0; lodStr += 4;
				if (from_chars(lodStr, lodStr + lodStrLength, lodIndex).ec != errc())
					continue; // TODO: should we throw an error if invalid lod index format?
				if (lodIndex >= tmpLods.size()) tmpLods.resize(lodIndex + 1);

				/*
				auto vertexBuffer = graphicsSystem->createBuffer(Buffer::Usage::Vertex | 
					Buffer::Usage::TransferDst, 
					Buffer::CpuAccess::None, )
				tmpLods[lodIndex].vertexBuffer = 
				*/
			}
			else nodes.push(child);
		}
	}

	return rootEntity;
}
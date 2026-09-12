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
#include "garden/system/render/model/translucent.hpp"
#include "garden/system/render/model/opaque.hpp"
#include "garden/system/render/model/cutout.hpp"
#include "garden/system/render/model/color.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/render/forward.hpp"
#include "garden/system/transform.hpp"
#include "garden/system/resource.hpp"
#include "garden/system/app-info.hpp"
#include "garden/system/log.hpp"
#include "garden/file.hpp"
#include "model/instance-data.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "meshoptimizer.h"

using namespace garden;

const vector<string_view> ModelRenderSystem::modelFileExts =
{
	".usd", ".usda", ".usdc", ".gltf", ".glb", ".fbx", ".obj"
};
const vector<ModelFileType> ModelRenderSystem::modelFileTypes =
{
	ModelFileType::USD, ModelFileType::USD, ModelFileType::USD, ModelFileType::glTF, ModelFileType::glTF,
	ModelFileType::FBX, ModelFileType::OBJ
};

void ModelRenderComponent::setLodCount(uint8 count)
{
	auto lodCount = getLodCount();
	if (count == lodCount)
		return;
	
	auto lodCapacity = getLodCapacity();
	if (count > lodCapacity)
	{
		if (lods) lods = realloc(lods, count);
		else lods = calloc<MeshLOD>(count);
		_setLodCapacity(count);
	}
	
	if (count > lodCount) // Note: assuming that MeshLOD can be zero initialized.
		memset((uint8*)lods + lodCount * sizeof(MeshLOD), 0, (count - lodCount) * sizeof(MeshLOD));
	_setLodCount(count);
}

void ModelRenderSystem::init()
{
	InstanceRenderSystem::init();

	auto manager = Manager::getInstance();
	ECSM_SUBSCRIBE_TO_EVENT("ImageLoaded", ModelRenderSystem::imageLoaded);

	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	ECSM_SUBSCRIBE_TO_EVENT("FileDrop", ModelRenderSystem::fileDrop);
	#endif

	#if GARDEN_DEBUG || GARDEN_EDITOR
	debugResourceName = pipelinePath.generic_string();
	#endif
}

//**********************************************************************************************************************
void ModelRenderSystem::imageLoaded()
{
	// TODO: update bindless pool and model components.
}

void ModelRenderSystem::fileDrop()
{
	#if GARDEN_DEBUG || GARDEN_EDITOR || !GARDEN_PACK_RESOURCES
	auto inputSystem = InputSystem::getInstance();
	auto& filePath = inputSystem->getCurrentFileDropPath();
	auto extension = filePath.extension();

	auto isModelFormat = false;
	for (auto modelFormat : modelFileExts)
	{
		if (extension == modelFormat)
		{
			isModelFormat = true;
			break;
		}
	}

	if (!isModelFormat)
		return;

	string_view resourcePath;
	if (InputSystem::getResourcePath("models/", filePath.generic_string(), resourcePath))
		loadModel(resourcePath);
	#endif
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
	modelData->uvSize = modelRenderView->getUvSize();
	modelData->uvOffset = modelRenderView->getUvOffset();
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
	if (componentView->getUvSize() != half2::one)
		serializer.write("uvSize", componentView->getUvSize());
	if (componentView->getUvOffset() != half2::zero)
		serializer.write("uvOffset", componentView->getUvOffset());
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
	deserializer.read("uvSize", componentView->_uvSize());
	deserializer.read("uvOffset", componentView->_uvOffset());
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
		componentView->_uvSize() = (half2)lerp((float2)frameA->uvSize, (float2)frameB->uvSize, t);
	if (frameA->animateUvOffset)
		componentView->_uvOffset() = (half2)lerp((float2)frameA->uvOffset, (float2)frameB->uvOffset, t);
	if (frameA->animateColorAdd)
		componentView->colorAdd = lerp(frameA->colorAdd, frameB->colorAdd, t);
	if (frameA->animateColorMul)
		componentView->colorMul = lerp(frameA->colorMul, frameB->colorMul, t);
	if (frameA->animateOrmAdd)
		componentView->ormAdd = lerp(frameA->ormAdd, frameB->ormAdd, t);
	if (frameA->animateOrmMul)
		componentView->ormMul = lerp(frameA->ormMul, frameB->ormMul, t);
}

#if GARDEN_DEBUG || GARDEN_EDITOR || defined(GARDEN_MODEL_CONVERTER)
//**********************************************************************************************************************
static int32 getModelFilePath(const fs::path& modelPath, fs::path& filePath, ModelFileType& fileType)
{
	auto& appResourcesPath = AppInfoSystem::getInstance()->getResourcesPath();
	auto path = fs::path("models") / modelPath;

	int32 fileCount = 0;
	for (uint8 i = 0; i < (uint8)ModelRenderSystem::modelFileExts.size(); i++)
	{
		path.replace_extension(ModelRenderSystem::modelFileExts[i]);
		if (File::tryGetResourcePath(appResourcesPath, path, filePath))
		{
			fileType = ModelRenderSystem::modelFileTypes[i];
			fileCount++;
		}
	}

	return fileCount;
}
static uint8 combineVertexData(const aiMesh* mesh, vector<uint8>& vertices)
{
	
}
//**********************************************************************************************************************
ID<Entity> ModelRenderSystem::loadModel(const fs::path& path, const ModelComponents* components)
{
	GARDEN_ASSERT(!path.empty());

	if (!components)
	{
		static const ModelComponents defaultComponents =
		{
			{ "Opaque", typeid(OpaqueModelComponent) },
			{ "Cutout", typeid(CutoutModelComponent) },
			{ "Translucent", typeid(TransModelComponent) },
			{ "Color", typeid(ColorModelComponent) }
		};
		components = &defaultComponents;
	}
	else 
	{
		#if GARDEN_DEBUG
		GARDEN_ASSERT(!components->empty());
		for (const auto& pair : *components)
		{
			GARDEN_ASSERT(!pair.first.empty());
		}
		#endif
	}

	fs::path inputFilePath; ModelFileType inputFileType;
	auto fileCount = getModelFilePath(path, inputFilePath, inputFileType);
	if (fileCount == 0)
	{
		GARDEN_LOG_ERROR("3D model file does not exist. (path: " + path.generic_string() + ")");
		return {};
	}
	if (fileCount > 1)
	{
		GARDEN_LOG_ERROR("3D model file is ambiguous. (path: " + path.generic_string() + ")");
		return {};
	}

	constexpr uint32 processFlags = aiProcess_CalcTangentSpace | 
		aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_GenNormals |
		aiProcess_PopulateArmatureData | aiProcess_SortByPType | aiProcess_GenBoundingBoxes;

	Assimp::Importer importer;
	auto scene = importer.ReadFile(inputFilePath.generic_string().c_str(), processFlags);
	if (!scene)
	{
		GARDEN_LOG_ERROR("Invalid Assimp 3D model file data. (path: " +
			path.generic_string() + ", error: " + string(importer.GetErrorString()) + ")");
		return {};
	}

	auto manager = Manager::getInstance();
	auto graphicsSystem = GraphicsSystem::getInstance();
	auto meshes = scene->mMeshes; auto materials = scene->mMaterials;
	stack<aiNode*> nodes; nodes.push(scene->mRootNode);
	map<uint32, MeshLOD> sharedLods; ID<Entity> rootEntity = {}, lastEntity = {};

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

		auto childrenCount = node->mNumChildren;
		auto children = node->mChildren;

		for (uint32 i = 0; i < childrenCount; i++)
		{
			auto child = children[i];
			auto lodStr = strstr(child->mName.C_Str(), "_LOD");
			auto lodStrLength = lodStr ? strlen(lodStr) : 0;

			if (lodStrLength > 4)
			{
				uint8 lodIndex = 0; lodStr += 4;
				if (from_chars(lodStr, lodStr + lodStrLength, lodIndex).ec != errc())
				{
					GARDEN_LOG_ERROR("Invalid Assimp 3D model LOD format. ("
						"node: " + string(child->mName.C_Str()) + ", "
						"path: " + path.generic_string() + ")");
					continue;
				}

				auto meshIds = child->mMeshes;
				auto meshIdCount = child->mNumMeshes;

				for (uint32 j = 0; j < meshIdCount; j++)
				{
					auto meshId = meshIds[j]; auto mesh = meshes[meshId];
					if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE)
						continue; // Note: skipping non triangle primitives.

					if (!mesh->HasPositions() || !mesh->HasFaces())
					{
						GARDEN_LOG_ERROR("Missing Assimp 3D model attributes. ("
							"mesh: " + string(mesh->mName.C_Str()) + ", "
							"node: " + string(child->mName.C_Str()) + ", "
							"path: " + path.generic_string() + ")");
						continue;
					}

					auto materialId = mesh->mMaterialIndex;
					auto material = materials[materialId];
					auto componentType = components->begin()->second;
					auto componentResult = components->find(material->GetName().C_Str());

					if (componentResult != components->end())
						componentType = componentResult->second;
					else
					{
						GARDEN_LOG_ERROR("Unknown Assimp 3D model material type. ("
							"material: " + string(material->GetName().C_Str()) + ", "
							"node: " + string(child->mName.C_Str()) + ", "
							"path: " + path.generic_string() + ")");
					}
					
					bool isAdded;
					auto modelRenderView = View<ModelRenderComponent>(
						manager->getOrAdd(newEntity, componentType, isAdded));
					if (isAdded)
					{
						// TODO: get and set material params
					}

					auto lodResult = sharedLods.find(meshId);
					if (lodResult != sharedLods.end())
					{
						modelRenderView->setLod(lodResult->second, lodIndex);
						continue;
					}

					// TODO: combine vertex data

					//auto vertexBuffer = graphicsSystem->createBuffer(Buffer::Usage::Vertex | 
					//	Buffer::Usage::TransferDst, 
					//	Buffer::CpuAccess::None, );
					//tmpLods[lodIndex].vertexBuffer = 
				}
			}
			else nodes.push(child);
		}
	}

	return rootEntity;
}
#endif
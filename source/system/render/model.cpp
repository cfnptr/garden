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

#include "math/normal-mapping.hpp"
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

// Garden Mesh Container
#define GMC_VERSION_MAJOR 1
#define GMC_VERSION_MINOR 0
#define GMC_VERSION_PATCH 0

static constexpr const char gmcMagic[4] = { 'G', 'M', 'C', '\0' };

namespace garden::graphics
{
	struct GmcHeader final
	{
		uint8 versionMajor = GMC_VERSION_MAJOR;
		uint8 versionMinor = GMC_VERSION_MINOR;
		uint8 versionPatch = GMC_VERSION_PATCH;
		uint8 isLittleEndian = GARDEN_LITTLE_ENDIAN;
		uint32 indexCount = 0;
		uint32 vertexCount = 0;
		uint8 encodedIndices : 1;
		uint8 encodedVertPF : 1;
		uint8 encodedVertAttr : 1;
		uint8 _reserver0 : 5;
		uint8 _reserver1 = 0;
		uint16 _reserver2 = 0;

		GmcHeader() : encodedIndices(0), encodedVertPF(0), encodedVertAttr(0) { }
	};
};

static void loadMeshDataGMC(const void* data, psize dataSize, raw_vector<uint8>& indices, raw_vector<uint8>& vertices)
{
	static constexpr auto gmcHeaderSize = sizeof(gmcMagic) + sizeof(GmcHeader);
	if (dataSize <= gmcHeaderSize || memcmp(data, gmcMagic, sizeof(gmcMagic)) != 0)
		throw GardenError("Invalid GMC file data.");

	auto gmcHeader = *(const GmcHeader*)((const uint8*)data + sizeof(gmcMagic));
	if (gmcHeader.versionMajor != GMC_VERSION_MAJOR || gmcHeader.versionMinor != GMC_VERSION_MINOR)
		throw GardenError("Bad GMC file version.");
	if (gmcHeader.isLittleEndian != GARDEN_LITTLE_ENDIAN)
		throw GardenError("Bad GMC file endianness.");
	if (gmcHeader.vertexCount == 0 || gmcHeader.indexCount == 0)
		throw GardenError("Invalid GMC header data.");

	// TODO:
}

void ModelRenderComponent::setLodCount(uint8 count)
{
	auto currCount = getLodCount();
	if (count == currCount)
		return;
	
	auto lodCapacity = getLodCapacity();
	if (count > lodCapacity)
	{
		auto newLods = new MeshLOD[count];
		copy(lods, lods + count, newLods);
		delete[] lods; lods = newLods;
		_setLodCapacity(count);
	}
	
	if (count < currCount)
		fill(lods + count, lods + currCount, MeshLOD());
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
namespace garden::graphics
{
	struct MeshOptVertex
	{
		float3 position;
		float3 normal;
		float2 texCoords;
	};
}

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

template<typename I = uint32>
static void optimizeMeshData(const aiMesh* mesh, raw_vector<uint8>& indices, raw_vector<uint8>& positions, 
	raw_vector<uint8>& attributes, raw_vector<uint8>& tmp0, raw_vector<uint8>& tmp1)
{
	auto faceCount = mesh->mNumFaces;
	auto indexCount = faceCount * 3;
	auto vertexCount = mesh->mNumVertices;
	const auto meshFaces = mesh->mFaces;
	indices.resize(indexCount * sizeof(I));
	auto indexData = (I*)indices.data();
	uint32 faceIndexCount = 0;

	for (uint32 i = 0; i < faceCount; i++)
	{
		auto face = meshFaces[i];
		GARDEN_ASSERT(face.mNumIndices == 3);
		auto data = indexData + faceIndexCount;
		data[0] = (I)face.mIndices[0];
		data[1] = (I)face.mIndices[1];
		data[2] = (I)face.mIndices[2];
		faceIndexCount += 3;
	}
	meshopt_optimizeVertexCache(indexData, indexData, indexCount, vertexCount);
	GARDEN_ASSERT(indexCount == faceIndexCount);

	positions.resize(vertexCount * sizeof(MeshOptVertex));
	auto vertexData = (MeshOptVertex*)positions.data();
	const auto meshVertices = mesh->mVertices, meshNormals = mesh->mNormals;
	const auto meshTexCoords = mesh->mTextureCoords[0];
	const auto meshColors = mesh->HasVertexColors(0) ? mesh->mColors[0] : nullptr;

	for (uint32 i = 0; i < vertexCount; i++)
	{
		auto& vertex = vertexData[i];
		vertex.position = *(const float3*)(meshVertices + i);
		vertex.normal = *(const float3*)(meshNormals + i);
		vertex.texCoords = repeat(*(const float2*)(meshTexCoords));
	}

	meshopt_optimizeOverdraw(indexData, indexData, indexCount, 
		&vertexData[0].position.x, vertexCount, sizeof(MeshOptVertex), 1.05f);
	vertexCount = meshopt_optimizeVertexFetch(vertexData, indexData, 
		indexCount, vertexData, vertexCount, sizeof(MeshOptVertex));
	tmp0.resize(indexCount * sizeof(float4));
	meshopt_generateTangents((float*)tmp0.data(), indexData, indexCount, 
		&vertexData->position.x, vertexCount, sizeof(MeshOptVertex), &vertexData->normal.x, 
		sizeof(MeshOptVertex), &vertexData->texCoords.x, sizeof(MeshOptVertex));

	attributes.resize(vertexCount * sizeof(MeshVertexAttr));
	auto posFlagData = (MeshVertexPF*)positions.data();
	auto attributeData = (MeshVertexAttr*)attributes.data();
	auto tangentData = (const float4*)tmp0.data();
	auto encTangentData = (ushort3*)tmp0.data();
	auto aabb = Aabb(f32x4(*(const float3*)&mesh->mAABB.mMin), 
		f32x4(*(const float3*)&mesh->mAABB.mMax));
	auto invAabbSize = 1.0f / aabb.getSize();

	for (uint32 i = 0; i < vertexCount; i++)
	{
		auto vertex = vertexData[i]; auto encNormal = encodeNormalOct3(vertex.normal);
		auto& posFlag = posFlagData[i]; auto& attribs = attributeData[i];
		posFlag.position = (ushort3)fma(((f32x4)vertex.position - 
			aabb.getMin()) * invAabbSize, f32x4(UINT16_MAX), f32x4(0.5f));
		posFlag.flags = encNormal.z > 0.0f ? MeshVertexPF::Flags::NormalSign : MeshVertexPF::Flags::None;
		attribs.normal = (ushort2)fma((float2)encNormal, float2(UINT16_MAX), float2(0.5f));
		attribs.texCoords = (ushort2)fma(vertex.texCoords, float2(UINT16_MAX), float2(0.5f));
		attribs.color = meshColors ? (Color)(*(const float4*)&meshColors[i]) : Color::transparent;
	}
	for (uint32 i = 0; i < indexCount; i++)
	{
		auto index = indexData[i]; auto tangent = tangentData[i];
		auto encTangent3 = encodeNormalOct3((float3)tangent);
		auto encTangent2 = (ushort2)fma((float2)encTangent3, float2(UINT16_MAX), float2(0.5f));
		auto tangentSign = encTangent3.z > 0.0f ? MeshVertexPF::Flags::TangentSign : MeshVertexPF::Flags::None;
		auto bitangentSign = tangent.w > 0.0f ? MeshVertexPF::Flags::BitangentSign : MeshVertexPF::Flags::None;
		posFlagData[index].flags |= tangentSign | bitangentSign;
		attributeData[index].tangent = encTangent2;
		encTangentData[i] = ushort3(encTangent2, (uint16)bitangentSign);
	}

	positions.resize(vertexCount * sizeof(MeshVertexPF));
	tmp1.resize(vertexCount * sizeof(I));
	memset(tmp1.data(), 0xFF, tmp1.size());
	auto splitData = (I*)tmp1.data();

	for (uint32 i = 0; i < indexCount; i++) // TODO: check if splits logic works!
	{
		auto v = indexData[i]; auto encTangent = encTangentData[i];
		while (v != (I)~0u && attributeData[v].tangent != (ushort2)encTangent &&
			hasAnyFlag(posFlagData[v].flags, MeshVertexPF::Flags::BitangentSign) !=
			hasAnyFlag((MeshVertexPF::Flags)encTangent.z, MeshVertexPF::Flags::BitangentSign))
		{
			v = splitData[v];
		}

		if (v == (I)~0u)
		{
			v = vertexCount++; auto index = indexData[i];
			positions.resize(positions.size() + sizeof(MeshVertexPF));
			posFlagData = (MeshVertexPF*)positions.data();
			auto posFlag = posFlagData[v]; posFlag = posFlagData[index];
			unsetFlags(posFlag.flags, MeshVertexPF::Flags::BitangentSign);
			setFlags(posFlag.flags, (MeshVertexPF::Flags)encTangent.z);

			attributes.resize(attributes.size() + sizeof(MeshVertexAttr));
			attributeData = (MeshVertexAttr*)attributes.data();
			auto& attribute = attributeData[v]; attribute = attributeData[index];
			attribute.tangent = (ushort2)encTangent;
			
			tmp1.resize(tmp1.size() + sizeof(I));
			splitData = (I*)tmp1.data();
			splitData[v] = splitData[index];
			splitData[index] = v;
		}
		indexData[i] = v;
	}
}
static void processMeshData(const fs::path& filePath, const aiMesh* mesh, uint32 meshID, raw_vector<uint8>& indices, 
	raw_vector<uint8>& positions, raw_vector<uint8>& attributes, raw_vector<uint8>& tmp0, raw_vector<uint8>& tmp1)
{
	auto indexType = toIndexType(mesh->mNumFaces * 3);
	uint32 indexCount, vertexCount, encSize;

	if (indexType == IndexType::Uint32)
	{
		optimizeMeshData<uint32>(mesh, indices, positions, attributes, tmp0, tmp1);
		indexCount = indices.size() / sizeof(uint32); vertexCount = positions.size() / sizeof(MeshVertexPF);
		tmp1.resize(meshopt_encodeIndexBufferBound(indexCount, vertexCount));
		encSize = meshopt_encodeIndexBuffer(tmp1.data(), tmp1.size(), (const uint32*)indices.data(), indexCount);
	}
	else if (indexType == IndexType::Uint16)
	{
		optimizeMeshData<uint16>(mesh, indices, positions, attributes, tmp0, tmp1);
		indexCount = indices.size() / sizeof(uint16); vertexCount = positions.size() / sizeof(MeshVertexPF);
		tmp1.resize(meshopt_encodeIndexBufferBound(indexCount, vertexCount));
		encSize = meshopt_encodeIndexBuffer(tmp1.data(), tmp1.size(), (const uint16*)indices.data(), indexCount);
	}
	else abort();
	GARDEN_ASSERT(vertexCount == attributes.size() / sizeof(MeshVertexAttr));

	tmp0.resize(sizeof(GmcHeader));
	auto gmcHeader = (GmcHeader*)tmp0.data();

	*gmcHeader = GmcHeader();
	gmcHeader->indexCount = indexCount;
	gmcHeader->vertexCount = vertexCount;
	gmcHeader->encodedIndices = encSize < indices.size() ? 1 : 0;

	if (gmcHeader->encodedIndices)
	{
		tmp0.resize(tmp0.size() + encSize);
		memcpy(tmp0.data() + sizeof(GmcHeader), tmp1.data(), encSize);
	}
	else
	{
		tmp0.resize(tmp0.size() + indices.size());
		memcpy(tmp0.data() + sizeof(GmcHeader), indices.data(), indices.size());
	}
	gmcHeader = (GmcHeader*)tmp0.data();

	tmp1.resize(meshopt_encodeVertexBufferBound(vertexCount, sizeof(MeshVertexPF)));
	encSize = meshopt_encodeVertexBuffer(tmp1.data(), tmp1.size(), 
		positions.data(), vertexCount, sizeof(MeshVertexPF));
	gmcHeader->encodedVertPF = encSize < positions.size() ? 1 : 0;

	auto dataOffset = tmp0.size();
	if (gmcHeader->encodedVertPF)
	{
		tmp0.resize(tmp0.size() + encSize);
		memcpy(tmp0.data() + dataOffset, tmp1.data(), encSize);
	}
	else
	{
		tmp0.resize(tmp0.size() + indices.size());
		memcpy(tmp0.data() + dataOffset, positions.data(), indices.size());
	}
	gmcHeader = (GmcHeader*)tmp0.data();

	tmp1.resize(meshopt_encodeVertexBufferBound(vertexCount, sizeof(MeshVertexAttr)));
	encSize = meshopt_encodeVertexBuffer(tmp1.data(), tmp1.size(), 
		attributes.data(), vertexCount, sizeof(MeshVertexAttr));
	gmcHeader->encodedVertAttr = encSize < attributes.size() ? 1 : 0;

	dataOffset = tmp0.size();
	if (gmcHeader->encodedVertAttr)
	{
		tmp0.resize(tmp0.size() + encSize);
		memcpy(tmp0.data() + dataOffset, tmp1.data(), encSize);
	}
	else
	{
		tmp0.resize(tmp0.size() + indices.size());
		memcpy(tmp0.data() + dataOffset, attributes.data(), indices.size());
	}

	ResourceSystem::getInstance()->storeBuffer(filePath, tmp0, true);
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

	constexpr uint32 processFlags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | 
		aiProcess_GenNormals | aiProcess_PopulateArmatureData | aiProcess_SortByPType | aiProcess_GenBoundingBoxes;

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
	raw_vector<uint8> indices, positions, attributes, tmp0, tmp1;

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
					auto meshID = meshIds[j]; auto mesh = meshes[meshID];
					if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE)
						continue; // Note: skipping non triangle primitives.

					if (!mesh->HasPositions() || !mesh->HasFaces() || 
						!mesh->HasNormals() || !mesh->HasTextureCoords(0))
					{
						GARDEN_LOG_ERROR("Missing Assimp 3D model attributes. ("
							"mesh: " + string(mesh->mName.C_Str()) + ", "
							"node: " + string(child->mName.C_Str()) + ", "
							"path: " + path.generic_string() + ")");
						continue;
					}

					auto materialID = mesh->mMaterialIndex;
					auto material = materials[materialID];
					type_index componentType = typeid(OpaqueModelComponent);
					auto componentResult = components->find(material->GetName().C_Str());

					if (componentResult != components->end())
						componentType = componentResult->second;
					else
					{
						GARDEN_LOG_WARN("Unknown Assimp 3D model material type. ("
							"material: " + string(material->GetName().C_Str()) + ", "
							"node: " + string(child->mName.C_Str()) + ", "
							"path: " + path.generic_string() + ")");
					}
					
					bool isAdded;
					auto modelRenderView = View<ModelRenderComponent>(
						manager->getOrAdd(newEntity, componentType, isAdded));
					if (isAdded)
					{
						modelRenderView->aabb = Aabb(f32x4(*(const float3*)&mesh->mAABB.mMin), 
							f32x4(*(const float3*)&mesh->mAABB.mMax));
						// TODO: get and set material params
					}

					auto lodResult = sharedLods.find(meshID);
					if (lodResult != sharedLods.end())
					{
						modelRenderView->setLod(lodResult->second, lodIndex);
						continue;
					}

					auto filePath = path; filePath.replace_extension();
					filePath = "models" / filePath / to_string(meshID); filePath.replace_extension(".gmc");
					processMeshData(filePath, mesh, meshID, indices, positions, attributes, tmp0, tmp1);

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
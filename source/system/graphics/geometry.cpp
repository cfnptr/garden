//--------------------------------------------------------------------------------------------------
// Copyright 2022-2023 Nikita Fediuchin. All rights reserved.
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/graphics/geometry.hpp"
#include "garden/system/graphics/geometry/translucent.hpp"
#include "garden/system/graphics/geometry/opaque.hpp"
#include "garden/system/graphics/geometry/cutoff.hpp"
#include "garden/system/graphics/editor/geometry.hpp"
#include "garden/system/graphics/deferred.hpp"
#include "garden/system/resource.hpp"

using namespace garden;

namespace
{
	struct GeometryPC final
	{
		float4 baseColor;
		float4 emissive;
		float metallic;
		float roughness;
		float reflectance;
		uint32 instanceIndex;
	};
	struct GeometryShadowPC final
	{
		float4x4 mvp;
	};
}

//--------------------------------------------------------------------------------------------------
static void createInstanceBuffers(GraphicsSystem* graphicsSystem,
	uint64 bufferSize, vector<vector<ID<Buffer>>>& instanceBuffers)
{
	auto swapchainSize = graphicsSystem->getSwapchainSize();
	instanceBuffers.resize(swapchainSize);

	for (uint32 i = 0; i < swapchainSize; i++)
	{
		auto buffer = graphicsSystem->createBuffer(Buffer::Bind::Storage,
			Buffer::Access::SequentialWrite, bufferSize,
			Buffer::Usage::Auto, Buffer::Strategy::Size);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, buffer,
			"buffer.storage.geometry.instances" + to_string(i));
		instanceBuffers[i].push_back(buffer);
	}
}
static void destroyInstanceBuffers(GraphicsSystem* graphicsSystem,
	vector<vector<ID<Buffer>>>& instanceBuffers)
{
	for (auto& sets : instanceBuffers) graphicsSystem->destroy(sets[0]);
	instanceBuffers.clear();
}

//--------------------------------------------------------------------------------------------------
#if GARDEN_EDITOR
void* GeometryRenderSystem::editor = nullptr;
#endif

void GeometryRenderSystem::initialize()
{
	if (!pipeline) pipeline = createPipeline();
	createInstanceBuffers(getGraphicsSystem(), 16 * sizeof(InstanceData), instanceBuffers);
	
	#if GARDEN_EDITOR
	if (!editor) editor = new GeometryEditor(this);
	#endif
}
void GeometryRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	if (editor)
	{
		delete (GeometryEditor*)editor;
		editor = nullptr;
	}
	#endif
}

//--------------------------------------------------------------------------------------------------
bool GeometryRenderSystem::isDrawReady()
{
	auto graphicsSystem = getGraphicsSystem();
	auto pipelineView = graphicsSystem->get(pipeline);
	if (!pipelineView->isReady()) return false;

	if (!baseDescriptorSet)
	{
		auto uniforms = getBaseUniforms();
		baseDescriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, baseDescriptorSet,
			"descriptorSet.geometry.base");
		
		// TODO: we cant copy default images inside the render pass. FIX THIS!
		// Move to some preXXX pass where we are not inside render pass.
		uniforms = getDefaultUniforms(); 
		defaultDescriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms), 1);
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, defaultDescriptorSet,
			"descriptorSet.geometry.default");
	}
	
	return true;
}
void GeometryRenderSystem::prepareDraw(const float4x4& viewProj,
	ID<Framebuffer> framebuffer, uint32 drawCount)
{
	auto graphicsSystem = getGraphicsSystem();
	if (graphicsSystem->get(instanceBuffers[0][0])->getBinarySize() <
		drawCount * sizeof(InstanceData))
	{
		auto bufferSize = drawCount * sizeof(InstanceData);
		destroyInstanceBuffers(graphicsSystem, instanceBuffers);
		createInstanceBuffers(graphicsSystem, bufferSize, instanceBuffers);

		graphicsSystem->destroy(baseDescriptorSet);
		auto uniforms = getBaseUniforms();
		baseDescriptorSet = graphicsSystem->createDescriptorSet(
			pipeline, std::move(uniforms));
		SET_RESOURCE_DEBUG_NAME(graphicsSystem, baseDescriptorSet,
			"descriptorSet.geometry.base");
	}

	swapchainIndex = graphicsSystem->getSwapchainIndex();

	auto framebufferView = graphicsSystem->get(framebuffer);
	auto instanceBufferView = graphicsSystem->get(instanceBuffers[swapchainIndex][0]);
	pipelineView = graphicsSystem->get(pipeline);
	instanceMap = (InstanceData*)instanceBufferView->getMap();
	framebufferSize = framebufferView->getSize();
}
void GeometryRenderSystem::beginDraw(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(float4(float2(0), framebufferSize), taskIndex);
}

//--------------------------------------------------------------------------------------------------
void GeometryRenderSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto geometryComponent = (GeometryRenderComponent*)meshRenderComponent;
	if (!geometryComponent->vertexBuffer || !geometryComponent->indexBuffer) return;

	auto graphicsSystem = getGraphicsSystem();
	auto vertexBufferView = graphicsSystem->get(geometryComponent->vertexBuffer);
	auto indexBufferView = graphicsSystem->get(geometryComponent->indexBuffer);
	if (!vertexBufferView->isReady() || !indexBufferView->isReady()) return;

	ID<DescriptorSet> descriptorSet;
	if (!geometryComponent->descriptorSet)
	{
		if (!geometryComponent->baseColorMap && !geometryComponent->ormMap)
		{
			descriptorSet = geometryComponent->descriptorSet = defaultDescriptorSet;
		}
		else
		{
			auto isReady = true;

			if (geometryComponent->baseColorMap)
			{
				auto map = geometryComponent->baseColorMap;
				isReady &= getGraphicsSystem()->get(map)->isReady();
			}
			if (geometryComponent->ormMap)
			{
				auto map = geometryComponent->ormMap;
				isReady &= getGraphicsSystem()->get(map)->isReady();
			}

			if (isReady)
			{
				dsBufferLocker.lock();
				dsCreateBuffer.push_back(geometryComponent);
				dsBufferLocker.unlock();
			}

			descriptorSet = defaultDescriptorSet;
		}
	}
	else descriptorSet = geometryComponent->descriptorSet;

	auto& instance = instanceMap[drawIndex];
	instance.model = model;
	instance.mvp = viewProj * model;

	Pipeline::DescriptorData descriptorData[8]; uint8 descriptorCount = 0;
	// TODO: can we bind base descriptor set once at the beginDraw?
	descriptorData[descriptorCount++] = Pipeline::DescriptorData(
		baseDescriptorSet, 1, swapchainIndex);
	descriptorData[descriptorCount++] = Pipeline::DescriptorData(descriptorSet);
	appendDescriptorData(descriptorData, descriptorCount, geometryComponent);
	pipelineView->bindDescriptorSetsAsync(descriptorData, descriptorCount, taskIndex);

	auto pushConstants = pipelineView->getPushConstantsAsync<GeometryPC>(taskIndex);
	pushConstants->baseColor = geometryComponent->baseColorFactor;
	pushConstants->emissive = float4(geometryComponent->emissiveFactor, 0.0f); // .a is unused
	pushConstants->metallic = geometryComponent->metallicFactor;
	pushConstants->roughness = geometryComponent->roughnessFactor;
	pushConstants->reflectance = geometryComponent->reflectanceFactor;
	pushConstants->instanceIndex = drawIndex;
	pipelineView->pushConstantsAsync(taskIndex);

	pipelineView->drawIndexedAsync(taskIndex,
		geometryComponent->vertexBuffer, geometryComponent->indexBuffer,
		geometryComponent->indexType, geometryComponent->indexCount,
		geometryComponent->indexOffset);
}

//--------------------------------------------------------------------------------------------------
void GeometryRenderSystem::finalizeDraw(const float4x4& viewProj,
	ID<Framebuffer> framebuffer, uint32 drawCount)
{
	auto instanceBufferView = getGraphicsSystem()->get(instanceBuffers[swapchainIndex][0]);
	instanceBufferView->flush(drawCount * sizeof(InstanceData));
}

//--------------------------------------------------------------------------------------------------
void GeometryRenderSystem::render()
{
	// TODO: use bindless instead.
	if (!dsCreateBuffer.empty())
	{
		auto graphicsSystem = getGraphicsSystem();
		graphicsSystem->startRecording(CommandBufferType::Frame);

		for (auto geometryComponent : dsCreateBuffer)
		{
			View<Image> baseColorMapView, ormMapView;
			if (geometryComponent->baseColorMap)
			{
				baseColorMapView = graphicsSystem->get(geometryComponent->baseColorMap);
				baseColorMapView->generateMips();
			}
			else baseColorMapView = graphicsSystem->get(graphicsSystem->getWhiteTexture());
			if (geometryComponent->ormMap)
			{
				ormMapView = graphicsSystem->get(geometryComponent->ormMap);
				ormMapView->generateMips();
			}
			else ormMapView = graphicsSystem->get(graphicsSystem->getGreenTexture());

			map<string, DescriptorSet::Uniform> uniforms =
			{
				{ "baseColorMap", DescriptorSet::Uniform(
					baseColorMapView->getDefaultView()) },
				{ "ormMap", DescriptorSet::Uniform(ormMapView->getDefaultView()) }
			};

			auto descriptorSet = graphicsSystem->createDescriptorSet(
				pipeline, std::move(uniforms), 1);
			geometryComponent->descriptorSet = descriptorSet;

			#if GARDEN_DEBUG
			auto transformComponent = getManager()->get<TransformComponent>(
				geometryComponent->entity);
			SET_RESOURCE_DEBUG_NAME(graphicsSystem, descriptorSet,
				"descriptorSet." + transformComponent->name);
			#endif
		}
		
		dsCreateBuffer.clear();
		graphicsSystem->stopRecording();
	}
}

//--------------------------------------------------------------------------------------------------
void GeometryRenderSystem::recreateSwapchain(const SwapchainChanges& changes)
{
	if (changes.bufferCount)
	{
		auto graphicsSystem = getGraphicsSystem();
		auto bufferView = graphicsSystem->get(instanceBuffers[0][0]);
		auto bufferSize = bufferView->getBinarySize();
		destroyInstanceBuffers(graphicsSystem, instanceBuffers);
		createInstanceBuffers(graphicsSystem, bufferSize, instanceBuffers);

		if (baseDescriptorSet)
		{
			auto descriptorSetView = graphicsSystem->get(baseDescriptorSet);
			auto uniforms = getBaseUniforms();
			descriptorSetView->recreate(std::move(uniforms));
		}
	}
}

//--------------------------------------------------------------------------------------------------
map<string, DescriptorSet::Uniform> GeometryRenderSystem::getDefaultUniforms()
{
	auto graphicsSystem = getGraphicsSystem();
	auto whiteTexture = graphicsSystem->getWhiteTexture();
	auto greenTexture = graphicsSystem->getGreenTexture();
	auto whiteTextureView = graphicsSystem->get(whiteTexture);
	auto greenTextureView = graphicsSystem->get(greenTexture);
	map<string, DescriptorSet::Uniform> defaultUniforms =
	{
		{ "baseColorMap", DescriptorSet::Uniform(whiteTextureView->getDefaultView()) },
		{ "ormMap", DescriptorSet::Uniform(greenTextureView->getDefaultView()) }
	};
	return defaultUniforms;
}
map<string, DescriptorSet::Uniform> GeometryRenderSystem::getBaseUniforms()
{
	map<string, DescriptorSet::Uniform> baseUniforms =
	{ { "instance", DescriptorSet::Uniform(instanceBuffers) } };
	return baseUniforms;
}

void GeometryRenderSystem::destroyResources(GeometryRenderComponent* geometryComponent)
{
	auto graphicsSystem = getGraphicsSystem();
	if (geometryComponent->vertexBuffer.getRefCount() == 1)
		graphicsSystem->destroy(geometryComponent->vertexBuffer);
	if (geometryComponent->indexBuffer.getRefCount() == 1)
		graphicsSystem->destroy(geometryComponent->indexBuffer);
	if (geometryComponent->baseColorMap.getRefCount() == 1)
		graphicsSystem->destroy(geometryComponent->baseColorMap);
	if (geometryComponent->ormMap.getRefCount() == 1)
		graphicsSystem->destroy(geometryComponent->ormMap);
	if (geometryComponent->descriptorSet.getRefCount() == 1)
		graphicsSystem->destroy(geometryComponent->descriptorSet);
}

ID<GraphicsPipeline> GeometryRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline();
	return pipeline;
}

#if GARDEN_DEBUG
//--------------------------------------------------------------------------------------------------
namespace
{
	struct NodeLoadData final
	{
		map<string, Ref<Image>> textures; // TODO: share between other models too?
		shared_ptr<Model> model;
		Manager* manager = nullptr;
		GraphicsSystem* graphicsSystem = nullptr;
	};
}

static void loadNodeRecursive(NodeLoadData& loadData, Model::Node node, ID<Entity> parent)
{
	auto rootEntity = loadData.manager->createEntity();
	{
		auto transformComponent = loadData.manager->add<TransformComponent>(rootEntity);
		transformComponent->position = node.getPosition();
		transformComponent->scale = node.getScale();
		transformComponent->rotation = node.getRotation();
		transformComponent->setParent(parent);
		
		if (node.getName().length() > 0)
			transformComponent->name = string(node.getName());
	}

	if (node.hashMesh())
	{
		auto mesh = node.getMesh();
		auto primitiveCount = mesh.getPrimitiveCount();
		uint32 opaqueIndex = 0, translucentIndex = 0, cutoffIndex = 0;
		
		for (psize i = 0; i < primitiveCount; i++)
		{
			auto primitive = mesh.getPrimitive(i);
			if (!primitive.hasMaterial()) continue;

			auto alphaMode = primitive.getMaterial().getAlphaMode();
			if (alphaMode == Model::Material::AlphaMode::Opaque) opaqueIndex++;
			else if (alphaMode == Model::Material::AlphaMode::Mask) cutoffIndex++;
			else if (alphaMode == Model::Material::AlphaMode::Blend) translucentIndex++;
			else continue;
		}

		auto hasSubEntities = opaqueIndex > 1 ||
			translucentIndex > 1 || cutoffIndex > 1;
		opaqueIndex = translucentIndex = cutoffIndex = 0;
		vector<ID<Entity>> subEntities;

		for (uint32 i = 0; i < primitiveCount; i++)
		{
			auto primitive = mesh.getPrimitive(i);
			if (!primitive.hasMaterial() || primitive.getType() !=
				Model::Primitive::Type::Triangles) continue;
			// TODO: handle different primitive types

			auto positionIndex = primitive.getAttributeIndex(
				Model::Attribute::Type::Position);
			auto normalIndex = primitive.getAttributeIndex(
				Model::Attribute::Type::Normal);
			if (positionIndex < 0 || normalIndex < 0) continue;
			// TODO: generate normals and tangents if no in model.

			auto indices = primitive.getIndices();
			auto vertices = primitive.getAttribute(positionIndex).getAccessor();
			if (vertices.getCount() == 0 || indices.getCount() == 0 ||
				!vertices.hasAabb()) continue;
			// TODO: generate aabb inside attribute taking into account sparse.

			GraphicsPipeline::Index indexType;
			auto componentType = indices.getComponentType();
			if (componentType == Model::Accessor::ComponentType::R32U)
				indexType = GraphicsPipeline::Index::Uint32;
			else if (componentType == Model::Accessor::ComponentType::R16U)
				indexType = GraphicsPipeline::Index::Uint16;
			else continue;
			// TODO: convert uint8 to uint16 indices.

			auto material = primitive.getMaterial();
			auto alphaMode = material.getAlphaMode();
			ID<Entity> entity;
			
			if (hasSubEntities)
			{
				// TODO: Suboptimal. We can share the same transform for several render components.
				if (alphaMode == Model::Material::AlphaMode::Opaque)
				{
					opaqueIndex++;
					if (opaqueIndex < (uint32)subEntities.size())
						entity = subEntities[opaqueIndex];
					else entity = {};
				}
				else if (alphaMode == Model::Material::AlphaMode::Mask)
				{
					cutoffIndex++;
					if (cutoffIndex < (uint32)subEntities.size())
						entity = subEntities[cutoffIndex];
					else entity = {};
				}
				else if (alphaMode == Model::Material::AlphaMode::Blend)
				{
					translucentIndex++;
					if (translucentIndex < (uint32)subEntities.size())
						entity = subEntities[translucentIndex];
					else entity = {};
				}
				else continue;

				if (!entity)
				{
					entity = loadData.manager->createEntity();
					auto transformComponent =
						loadData.manager->add<TransformComponent>(entity);
					transformComponent->setParent(rootEntity);
					subEntities.push_back(entity);

					if (material.getName().length() > 0)
						transformComponent->name = string(material.getName());
				}
			}
			else entity = rootEntity;

			static const vector<Model::Attribute::Type> attributes =
			{
				Model::Attribute::Type::Position, 
				Model::Attribute::Type::Normal,
				Model::Attribute::Type::TexCoord,
			};

			// TODO: detect shared buffers and reuse them. Some sort of hash map.
			auto vertexBuffer = ResourceSystem::getInstance()->loadVertexBuffer(
				loadData.model, primitive, Buffer::Bind::Vertex |
				Buffer::Bind::TransferDst, attributes);
			auto indexBuffer = ResourceSystem::getInstance()->loadBuffer(
				loadData.model, indices, Buffer::Bind::Index |
				Buffer::Bind::TransferDst);
			SET_RESOURCE_DEBUG_NAME(loadData.graphicsSystem, vertexBuffer,
				"buffer.vertex." + (mesh.getName().length() > 0 ?
				string(mesh.getName()) : to_string(*entity)));
			SET_RESOURCE_DEBUG_NAME(loadData.graphicsSystem, indexBuffer,
				"buffer.index." + (mesh.getName().length() > 0 ?
				string(mesh.getName()) : to_string(*entity)));

			const uint8 downscaleCount = 0;//3;
			const auto flags = Image::Bind::Sampled |
				Image::Bind::TransferSrc | Image::Bind::TransferDst;
			auto& directoryPath = loadData.model->getRelativePath();
			Ref<Image> baseColorMap = {}, ormMap = {};
			
			if (material.hasBaseColorTexture())
			{
				auto texture = material.getBaseColorTexture();
				auto texturePath = string(texture.getPath());
				auto searchResult = loadData.textures.find(texturePath);
				if (searchResult == loadData.textures.end())
				{
					baseColorMap = ResourceSystem::getInstance()->loadImage(
						directoryPath / texturePath, flags, 0,
						downscaleCount, Buffer::Strategy::Size, false);
					SET_RESOURCE_DEBUG_NAME(loadData.graphicsSystem, baseColorMap,
						"image.baseColor." + (texture.getName().length() > 0 ?
						string(texture.getName()) : to_string(*entity)));
					loadData.textures.emplace(std::move(texturePath), baseColorMap);
				}
				else baseColorMap = searchResult->second;
			}
			if (material.hasOrmTexture())
			{
				auto texture = material.getOrmTexture();
				auto texturePath = string(texture.getPath());
				auto searchResult = loadData.textures.find(texturePath);
				if (searchResult == loadData.textures.end())
				{
					ormMap = ResourceSystem::getInstance()->loadImage(
						directoryPath / texturePath, flags, 0,
						downscaleCount, Buffer::Strategy::Size, true);
					SET_RESOURCE_DEBUG_NAME(loadData.graphicsSystem, ormMap,
						"image.omr." + (texture.getName().length() > 0 ?
						string(texture.getName()) : to_string(*entity)));
					loadData.textures.emplace(std::move(texturePath), ormMap);
				}
				else ormMap = searchResult->second;
			}

			// TODO: we need to recompute tangents due to different coordinate spaces.
			// Use MikkTSpace algorithm.
			// TODO: also load texture sampler. (wrap, filter, etc...)

			GeometryRenderComponent* geometryComponent;
			GeometryShadowRenderComponent* geometryShadowComponent;

			if (alphaMode == Model::Material::AlphaMode::Opaque)
			{
				geometryComponent = *loadData.manager->add<
					OpaqueRenderComponent>(entity);
				geometryShadowComponent = *loadData.manager->add<
					OpaqueShadowRenderComponent>(entity);
			}
			else if (alphaMode ==  Model::Material::AlphaMode::Blend)
			{
				geometryComponent = *loadData.manager->add<
					TranslucentRenderComponent>(entity);
			}
			else
			{
				auto component = loadData.manager->add<
					CutoffRenderComponent>(entity);
				component->alphaCutoff = material.getAlphaCutoff();
				geometryComponent = *component;
			}

			geometryComponent->aabb = vertices.getAabb();
			geometryComponent->indexType = indexType;
			geometryComponent->indexCount = (uint32)indices.getCount();
			geometryComponent->vertexBuffer = std::move(vertexBuffer);
			geometryComponent->indexBuffer = std::move(indexBuffer);
			geometryComponent->baseColorMap = baseColorMap;
			geometryComponent->ormMap = ormMap;
			geometryComponent->baseColorFactor = material.getBaseColorFactor();
			geometryComponent->emissiveFactor = material.getEmissiveFactor();
			geometryComponent->metallicFactor = material.getMetallicFactor();
			geometryComponent->roughnessFactor = material.getRoughnessFactor();
			// TODO: geometryComponent->reflectance = toRefl(material.getIOR());

			if (alphaMode == Model::Material::AlphaMode::Opaque) // TODO: tmp
			{
				geometryShadowComponent->aabb = geometryComponent->aabb;
				geometryShadowComponent->indexType = geometryComponent->indexType;
				geometryShadowComponent->indexCount = geometryComponent->indexCount;
				geometryShadowComponent->vertexBuffer = geometryComponent->vertexBuffer;
				geometryShadowComponent->indexBuffer = geometryComponent->indexBuffer;
			}
		}
	}

	// TODO: load lights and cameras.

	auto childrenCount = node.getChildrenCount();
	for (uint32 i = 0; i < childrenCount; i++)
		loadNodeRecursive(loadData, node.getChildren(i), rootEntity);
}

//--------------------------------------------------------------------------------------------------
ID<Entity> GeometryRenderSystem::loadModel(const fs::path& path, uint32 sceneIndex)
{
	GARDEN_ASSERT(!path.empty());
	NodeLoadData loadData;
	loadData.manager = getManager();
	loadData.model = ResourceSystem::getInstance()->loadModel(path);
	loadData.graphicsSystem = getGraphicsSystem();

	auto scene = loadData.model->getScene(sceneIndex);
	auto rootEntity = loadData.manager->createEntity();
	auto transformComponent = loadData.manager->add<TransformComponent>(rootEntity);

	if (scene.getName().length() > 0)
		transformComponent->name = scene.getName();
	else transformComponent->name = path.generic_string();

	auto nodeCount = scene.getNodeCount();
	if (nodeCount == 0) return rootEntity;
	
	for (uint32 i = 0; i < nodeCount; i++)
		loadNodeRecursive(loadData, scene.getNode(i), rootEntity);
	return rootEntity;
}
#endif

//--------------------------------------------------------------------------------------------------
#if GARDEN_EDITOR
void* GeometryShadowRenderSystem::editor = nullptr;
#endif

void GeometryShadowRenderSystem::initialize()
{
	if (!pipeline) pipeline = createPipeline();

	#if GARDEN_EDITOR
	if (!editor) editor = new GeometryShadowEditor(this);
	#endif
}
void GeometryShadowRenderSystem::terminate()
{
	#if GARDEN_EDITOR
	if (editor)
	{
		delete (GeometryShadowEditor*)editor;
		editor = nullptr;
	}
	#endif
}

//--------------------------------------------------------------------------------------------------
bool GeometryShadowRenderSystem::isDrawReady()
{
	auto pipelineView = getGraphicsSystem()->get(pipeline);
	return pipelineView->isReady();
}
void GeometryShadowRenderSystem::prepareDraw(const float4x4& viewProj,
	ID<Framebuffer> framebuffer, uint32 drawCount)
{
	auto graphicsSystem = getGraphicsSystem();
	auto framebufferView = graphicsSystem->get(framebuffer);
	pipelineView = graphicsSystem->get(pipeline);
	pipelineView->updateFramebuffer(framebuffer);
	framebufferSize = framebufferView->getSize();
}
void GeometryShadowRenderSystem::beginDraw(int32 taskIndex)
{
	pipelineView->bindAsync(0, taskIndex);
	pipelineView->setViewportScissorAsync(float4(float2(0), framebufferSize), taskIndex);
}

//--------------------------------------------------------------------------------------------------
void GeometryShadowRenderSystem::draw(MeshRenderComponent* meshRenderComponent,
	const float4x4& viewProj, const float4x4& model, uint32 drawIndex, int32 taskIndex)
{
	auto geometryShadowComponent = (GeometryShadowRenderComponent*)meshRenderComponent;
	if (!geometryShadowComponent->vertexBuffer || !geometryShadowComponent->indexBuffer) return;

	auto graphicsSystem = getGraphicsSystem();
	auto vertexBufferView = graphicsSystem->get(geometryShadowComponent->vertexBuffer);
	auto indexBufferView = graphicsSystem->get(geometryShadowComponent->indexBuffer);
	if (!vertexBufferView->isReady() || !indexBufferView->isReady()) return;

	auto pushConstants = pipelineView->getPushConstantsAsync<GeometryShadowPC>(taskIndex);
	pushConstants->mvp = viewProj * model;
	pipelineView->pushConstantsAsync(taskIndex);

	pipelineView->drawIndexedAsync(taskIndex,
		geometryShadowComponent->vertexBuffer, geometryShadowComponent->indexBuffer,
		geometryShadowComponent->indexType, geometryShadowComponent->indexCount,
		geometryShadowComponent->indexOffset);
}

//--------------------------------------------------------------------------------------------------
void GeometryShadowRenderSystem::destroyResources(
	GeometryShadowRenderComponent* geometryShadowComponent)
{
	auto graphicsSystem = getGraphicsSystem();
	if (geometryShadowComponent->vertexBuffer.getRefCount() == 1)
		graphicsSystem->destroy(geometryShadowComponent->vertexBuffer);
	if (geometryShadowComponent->indexBuffer.getRefCount() == 1)
		graphicsSystem->destroy(geometryShadowComponent->indexBuffer);
}

ID<GraphicsPipeline> GeometryShadowRenderSystem::getPipeline()
{
	if (!pipeline) pipeline = createPipeline();
	return pipeline;
}
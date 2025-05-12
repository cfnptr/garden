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

#include "garden/editor/system/physics-renderer.hpp"
#include "garden/system/graphics.hpp"
#include "garden/system/render/deferred.hpp"
#include "garden/system/resource.hpp"

// TODO: Implement faster debug renderer using JPH::DebugRender.

#ifdef JPH_DEBUG_RENDERER

using namespace garden;
using namespace garden::physics;

static ID<Buffer> createVertexBuffer(uint64 size, const void* data)
{
	auto buffer = GraphicsSystem::Instance::get()->createBuffer(Buffer::Usage::Vertex | Buffer::Usage::TransferDst,
		Buffer::CpuAccess::None, data, size, Buffer::Location::PreferGPU, Buffer::Strategy::Default);
	SET_RESOURCE_DEBUG_NAME(buffer, "buffer.vertex.physicsDebug" + to_string(*buffer));
	return buffer;
}

bool PhysicsDebugRenderer::isReady()
{
	auto result = true;
	if (linePipeline)
	{
		auto pipelineView = GraphicsSystem::Instance::get()->get(linePipeline);
		result &= pipelineView->isReady();
	}
	if (trianglePipeline)
	{
		auto pipelineView = GraphicsSystem::Instance::get()->get(trianglePipeline);
		result &= pipelineView->isReady();
	}
	return result;
}

//**********************************************************************************************************************
void PhysicsDebugRenderer::drawLines(const f32x4x4& viewProj)
{
	if (lines.empty())
		return;

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (!linePipeline)
	{
		linePipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/physics/lines", deferredSystem->getDepthLdrFramebuffer());
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(linePipeline);
	if (!pipelineView->isReady())
		return;

	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->mvp = (float4x4)viewProj;

	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->pushConstants();
	pipelineView->draw(linesBuffer, (uint32)lines.size() * 2);
	graphicsSystem->destroy(linesBuffer);
	linesBuffer = {};
	lines.clear();
}
void PhysicsDebugRenderer::drawTriangles(const f32x4x4& viewProj)
{
	if (triangles.empty())
		return;

	auto deferredSystem = DeferredRenderSystem::Instance::get();
	if (!trianglePipeline)
	{
		trianglePipeline = ResourceSystem::Instance::get()->loadGraphicsPipeline(
			"editor/physics/triangles", deferredSystem->getDepthLdrFramebuffer());
	}

	auto graphicsSystem = GraphicsSystem::Instance::get();
	auto pipelineView = graphicsSystem->get(trianglePipeline);
	if (!pipelineView->isReady())
		return;

	auto pushConstants = pipelineView->getPushConstants<PushConstants>();
	pushConstants->mvp = (float4x4)viewProj;

	pipelineView->bind();
	pipelineView->setViewportScissor();
	pipelineView->pushConstants();
	pipelineView->draw(trianglesBuffer, (uint32)triangles.size());
	graphicsSystem->destroy(trianglesBuffer);
	trianglesBuffer = {};
	triangles.clear();
}

void PhysicsDebugRenderer::preDraw()
{
	if (!lines.empty())
		linesBuffer = createVertexBuffer(lines.size() * sizeof(Line), lines.data());
	if (!triangles.empty())
		trianglesBuffer = createVertexBuffer(triangles.size() * sizeof(Triangle), triangles.data());
}
void PhysicsDebugRenderer::draw(const f32x4x4& viewProj)
{
	drawLines(viewProj);
	drawTriangles(viewProj);
}

//**********************************************************************************************************************
void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
{
	Line line;
	JPH::Vec3(inFrom - cameraPosition).StoreFloat3(&line.from);
	line.fromColor = inColor;
	JPH::Vec3(inTo - cameraPosition).StoreFloat3(&line.to);
	line.toColor = inColor;
	lines.push_back(line);
}
void PhysicsDebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, 
	JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow)
{
	Triangle triangle(inV1 - cameraPosition, 
		inV2 - cameraPosition, inV3 - cameraPosition, inColor);
	triangles.push_back(triangle);
}
void PhysicsDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, 
	const string_view& inString, JPH::ColorArg inColor, float inHeight)
{
	// TODO: implement when will be porting Uran engine text rendering.
}

#endif // JPH_DEBUG_RENDERER
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

#pragma once

#ifdef JPH_DEBUG_RENDERER
#include "garden/graphics/pipeline/graphics.hpp"
#include "garden/system/physics-impl.hpp"
#include "Jolt/Renderer/DebugRendererSimple.h"

namespace garden
{

using namespace physics;
using namespace garden::graphics;

//**********************************************************************************************************************
class PhysicsDebugRenderer final : public JPH::DebugRendererSimple
{
public:
	struct PushConstants final
	{
		float4x4 mvp;
	};
	struct Line final
	{
		JPH::Float3 from;
		JPH::Color fromColor;
		JPH::Float3 to;
		JPH::Color toColor;
	};
private:
	vector<Line> lines;
	vector<Triangle> triangles;
	ID<GraphicsPipeline> linePipeline = {};
	ID<GraphicsPipeline> trianglePipeline = {};
	ID<Buffer> linesBuffer = {};
	ID<Buffer> trianglesBuffer = {};
	JPH::RVec3 cameraPosition;
public:
	void setCameraPosition(f32x4 cameraPosition)
	{
		this->cameraPosition = toRVec3(cameraPosition);
		SetCameraPos(this->cameraPosition);
	}

	bool isReady();
	void drawLines(const f32x4x4& viewProj);
	void drawTriangles(const f32x4x4& viewProj);
	void preDraw();
	void draw(const f32x4x4& viewProj);

	void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) final;
	void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, 
		JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) final;
	void DrawText3D(JPH::RVec3Arg inPosition, const string_view& inString, 
		JPH::ColorArg inColor, float inHeight) final;
};

} // namespace garden
#endif // JPH_DEBUG_RENDERER
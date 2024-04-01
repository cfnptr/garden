// Copyright 2022-2024 Nikita Fediuchin. All rights reserved.
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
#include "garden/system/render/geometry.hpp"

#if GARDEN_EDITOR
namespace garden
{

using namespace garden;
using namespace garden::graphics;

class GeometryEditor final
{
	GeometryEditor(GeometryRenderSystem* system);
	friend class GeometryRenderSystem;
public:
	void renderInfo(GeometryRenderComponent* geometryComponent, float* alphaCutoff);
};

class GeometryShadowEditor final
{
	GeometryShadowEditor(GeometryShadowRenderSystem* system);
	friend class GeometryShadowRenderSystem;
public:
	void renderInfo(GeometryShadowRenderComponent* geometryShadowComponent);
};

} // namespace garden
#endif
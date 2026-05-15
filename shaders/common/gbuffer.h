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

#ifndef G_BUFFER_H
#define G_BUFFER_H

#define G_BUFFER_BASE_COLOR 0
#define G_BUFFER_MATERIAL_ID 0
#define G_BUFFER_EM_COLOR 0
#define G_BUFFER_EM_FACTOR 1
#define G_BUFFER_METALLIC 1
#define G_BUFFER_ROUGHNESS 1
#define G_BUFFER_AMBIENT_OCCL 1
#define G_BUFFER_SHADOW 1
#define G_BUFFER_CLEAR_COAT 1
#define G_BUFFER_CC_ROUGHNESS 1
#define G_BUFFER_REFLECTANCE 2
#define G_BUFFER_SPEC_FACTOR 2
#define G_BUFFER_NORMALS 2
#define G_BUFFER_VELOCITY 3
#define G_BUFFER_COUNT 4

#define G_MATERIAL_BASE 0x00
#define G_MATERIAL_SPECULAR 0x01
#define G_MATERIAL_EMISSIVE 0x02
#define G_MATERIAL_CLEAR_COAT 0x04
#define G_MATERIAL_SUBSURFACE 0x08
#define G_MATERIAL_SHEEN 0x10

#endif // GBUFFER_GSL
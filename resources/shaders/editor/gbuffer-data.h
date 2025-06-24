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

#ifndef G_BUFFER_DATA_H
#define G_BUFFER_DATA_H

#define G_BUFFER_DRAW_MODE_OFF 0
#define G_BUFFER_DRAW_MODE_LIGHTING 1
#define G_BUFFER_DRAW_MODE_BASE_COLOR 2
#define G_BUFFER_DRAW_MODE_SPECULAR_FACTOR 3
#define G_BUFFER_DRAW_MODE_TRANSMISSION 4
#define G_BUFFER_DRAW_MODE_METALLIC 5
#define G_BUFFER_DRAW_MODE_ROUGHNESS 6
#define G_BUFFER_DRAW_MODE_MATERIAL_AO 7
#define G_BUFFER_DRAW_MODE_REFLECTANCE 8
#define G_BUFFER_DRAW_MODE_CC_ROUGHNESS 9
#define G_BUFFER_DRAW_MODE_NORMALS 10
#define G_BUFFER_DRAW_MODE_MATERIAL_SHADOWS 11
#define G_BUFFER_DRAW_MODE_EMISSIVE_COLOR 12
#define G_BUFFER_DRAW_MODE_EMISSIVE_FACTOR 13
#define G_BUFFER_DRAW_MODE_GI_COLOR 14
#define G_BUFFER_DRAW_MODE_HDR_BUFFER 15
#define G_BUFFER_DRAW_MODE_OIT_ACCUM_COLOR 16
#define G_BUFFER_DRAW_MODE_OIT_ACCUM_ALPHA 17
#define G_BUFFER_DRAW_MODE_OIT_REVEAL 18
#define G_BUFFER_DRAW_MODE_DEPTH_BUFFER 19
#define G_BUFFER_DRAW_MODE_WORLD_POSITION 20
#define G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_COLOR 21
#define G_BUFFER_DRAW_MODE_GLOBAL_SHADOW_ALPHA 22
#define G_BUFFER_DRAW_MODE_GLOBAL_D_SHADOW_COLOR 23
#define G_BUFFER_DRAW_MODE_GLOBAL_D_SHADOW_ALPHA 24
#define G_BUFFER_DRAW_MODE_GLOBAL_AO 25
#define G_BUFFER_DRAW_MODE_GLOBAL_D_AO 26
#define G_BUFFER_DRAW_MODE_GLOBAL_REFLECTIONS 27
#define G_BUFFER_DRAW_MODE_COUNT 28

#ifdef __GARDEN__
static const char* G_BUFFER_DRAW_MODE_NAMES[G_BUFFER_DRAW_MODE_COUNT] =
{
	"Off", "Lighting", "Base Color", "Specular Factor", "Transmission", 
	"Metallic", "Roughness", "Material AO", "Reflectance", "Clear Coat Roughness", 
	"Normals", "Material Shadows", "Emissive Color", "Emissive Factor", 
	"GI Color", "HDR Buffer", "OIT Accum Color", "OI Accum Alpha",
	"OIT Revealage", "Depth Buffer", "World Positions", "Global Shadow Color",
	"Global Shadow Alpha", "Global Denoised Shadow Color", 
	"Global Denoised Shadow Alpha", "Global AO", "Global Denoised AO",
	"Global Reflections"
};
#endif

#endif // G_BUFFER_DATA_H
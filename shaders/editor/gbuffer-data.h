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

#ifndef G_BUFFER_DATA_H
#define G_BUFFER_DATA_H

#define G_BUFFER_DRAW_MODE_OFF 0
#define G_BUFFER_DRAW_MODE_LIGHTING_DEBUG 1
#define G_BUFFER_DRAW_MODE_HDR_BUFFER 2
#define G_BUFFER_DRAW_MODE_DEPTH_BUFFER 3
#define G_BUFFER_DRAW_MODE_NORMAL_BUFFER 4
#define G_BUFFER_DRAW_MODE_REFLECTION_BUFFER 5
#define G_BUFFER_DRAW_MODE_SHADOW_COLOR 6
#define G_BUFFER_DRAW_MODE_SHADOW_ALPHA 7
#define G_BUFFER_DRAW_MODE_AO_BUFFER 8
#define G_BUFFER_DRAW_MODE_GI_BUFFER 9
#define G_BUFFER_DRAW_MODE_OIT_COLOR 10
#define G_BUFFER_DRAW_MODE_OIT_ALPHA 11
#define G_BUFFER_DRAW_MODE_OIT_REVEAL 12

#define G_BUFFER_DRAW_MODE_BASE_COLOR 13
#define G_BUFFER_DRAW_MODE_SPECULAR_FACTOR 14
#define G_BUFFER_DRAW_MODE_METALLIC 15
#define G_BUFFER_DRAW_MODE_ROUGHNESS 16
#define G_BUFFER_DRAW_MODE_MATERIAL_AO 17
#define G_BUFFER_DRAW_MODE_REFLECTANCE 18
#define G_BUFFER_DRAW_MODE_MATERIAL_SHADOW 19
#define G_BUFFER_DRAW_MODE_VELOCITY 20
#define G_BUFFER_DRAW_MODE_DISOCCLUSION 21

#define G_BUFFER_DRAW_MODE_BLURRED_SHADOW_COLOR 22
#define G_BUFFER_DRAW_MODE_BLURRED_SHADOW_ALPHA 23
#define G_BUFFER_DRAW_MODE_BLURRED_AO 24
#define G_BUFFER_DRAW_MODE_WORLD_POSITION 25
#define G_BUFFER_DRAW_MODE_HDR_LUMA 26
#define G_BUFFER_DRAW_MODE_COUNT 27

#ifdef __GARDEN__
static const char* G_BUFFER_DRAW_MODE_NAMES[G_BUFFER_DRAW_MODE_COUNT] =
{
	"Off", "Lighting Debug", "HDR Buffer", "Depth Buffer", "Normal Buffer", 
	"Reflection Buffer", "Shadow Color", "Shadow Alpha", "AO Buffer", 
	"GI Buffer", "OIT Color", "OIT Alpha", "OIT Revealage", 

	"Base Color", "Specular Factor", "Metallic", "Roughness", "Material AO", 
	"Reflectance", "Material Shadow", "Velocity", "Disocclusion",

	"Blurred Shadow Color", "Blurred Shadow Alpha", 
	"Blurred AO", "World Position", "HDR Luma"
};
#endif

#endif // G_BUFFER_DATA_H
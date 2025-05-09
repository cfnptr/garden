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

/***********************************************************************************************************************
 * @file
 */

#pragma once
#include "garden/defines.hpp"
#include "math/types.hpp"

namespace garden
{

/**
 * @brief Image file format types.
 */
enum class ImageFileType : uint8
{
	Webp, Png, Jpg, Exr, Hdr, Bmp, Psd, Tga, Pic, Gif, Count
};

/**
 * @brief Returns image file type.
 * @param name target file type name
 * @throw GardenError on unknown image file type.
 */
static ImageFileType toImageFileType(string_view name)
{
	if (name == "webp") return ImageFileType::Webp;
	if (name == "png") return ImageFileType::Png;
	if (name == "jpg" || name == "jpeg") return ImageFileType::Jpg;
	if (name == "exr") return ImageFileType::Exr;
	if (name == "hdr") return ImageFileType::Hdr;
	if (name == "bmp") return ImageFileType::Bmp;
	if (name == "psd") return ImageFileType::Psd;
	if (name == "tga") return ImageFileType::Tga;
	if (name == "pic") return ImageFileType::Pic;
	if (name == "gif") return ImageFileType::Gif;
	throw GardenError("Unknown image file type. (name: " + string(name) + ")");
}

};
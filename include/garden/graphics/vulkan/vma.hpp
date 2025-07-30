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

// Note: No pragma once here.

#define VK_ENABLE_BETA_EXTENSIONS

#include "volk.h"
#undef VK_NO_PROTOTYPES

#include "vulkan/vulkan.hpp"

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#include "vk_mem_alloc.h"

#if __clang__
#pragma clang diagnostic pop
#endif

// Xlib why? Why you globally define these values?!
#ifdef _X11_XLIB_H_
#undef None
#undef Always
#undef Bool
#endif
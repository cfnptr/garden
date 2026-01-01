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

/***********************************************************************************************************************
 * @file
 * @brief All user interface systems.
 */

#pragma once
#include "garden/system/ui/label.hpp"
#include "garden/system/ui/input.hpp"
#include "garden/system/ui/button.hpp"
#include "garden/system/ui/trigger.hpp"
#include "garden/system/ui/scissor.hpp"
#include "garden/system/ui/checkbox.hpp"
#include "garden/system/ui/transform.hpp"

#if GARDEN_EDITOR
#include "garden/editor/system/ui/label.hpp"
#include "garden/editor/system/ui/input.hpp"
#include "garden/editor/system/ui/button.hpp"
#include "garden/editor/system/ui/trigger.hpp"
#include "garden/editor/system/ui/scissor.hpp"
#include "garden/editor/system/ui/checkbox.hpp"
#include "garden/editor/system/ui/transform.hpp"
#endif
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

#include "garden/graphics/common.hpp"
#include "garden/graphics/vulkan.hpp"

using namespace std;
using namespace math;
using namespace garden::graphics;

#if GARDEN_DEBUG
void DebugLabel::begin(const string& name, Color color)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	BeginLabelCommand command;
	command.name = name.c_str();
	command.color = color;
	GraphicsAPI::currentCommandBuffer->commandMutex.lock();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	GraphicsAPI::currentCommandBuffer->commandMutex.unlock();
}
void DebugLabel::end()
{
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	EndLabelCommand command;
	GraphicsAPI::currentCommandBuffer->commandMutex.lock();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	GraphicsAPI::currentCommandBuffer->commandMutex.unlock();
}
void DebugLabel::insert(const string& name, Color color)
{
	GARDEN_ASSERT(!name.empty());
	GARDEN_ASSERT(GraphicsAPI::currentCommandBuffer);
	InsertLabelCommand command;
	command.name = name.c_str();
	command.color = color;
	GraphicsAPI::currentCommandBuffer->commandMutex.lock();
	GraphicsAPI::currentCommandBuffer->addCommand(command);
	GraphicsAPI::currentCommandBuffer->commandMutex.unlock();
}
#endif
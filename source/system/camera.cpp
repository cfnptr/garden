//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "garden/system/camera.hpp"
#include "garden/system/editor/camera.hpp"

using namespace garden;

float4x4 CameraComponent::calcProjection() const noexcept
{
	if (type == Type::Perspective)
	{
		return calcPerspProjInfRevZ(p.perspective.fieldOfView,
			p.perspective.aspectRatio, p.perspective.nearPlane);
	}
	
	return calcOrthoProj(p.orthographic.width,
		p.orthographic.height, p.orthographic.depth);
}

//--------------------------------------------------------------------------------------------------
void CameraSystem::initialize()
{
	#if GARDEN_EDITOR
	editor = new CameraEditor(this);
	#endif
}
void CameraSystem::terminate()
{
	#if GARDEN_EDITOR
	delete (CameraEditor*)editor;
	#endif
}

//--------------------------------------------------------------------------------------------------
type_index CameraSystem::getComponentType() const
{
	return typeid(CameraComponent);
}
ID<Component> CameraSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void CameraSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<CameraComponent>(instance));
}
View<Component> CameraSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CameraComponent>(instance)));
}
void CameraSystem::disposeComponents() { components.dispose(); }
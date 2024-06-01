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

#include "garden/system/camera.hpp"

using namespace garden;

//**********************************************************************************************************************
float4x4 CameraComponent::calcProjection() const noexcept
{
	if (type == ProjectionType::Perspective)
	{
		return calcPerspProjInfRevZ(p.perspective.fieldOfView,
			p.perspective.aspectRatio, p.perspective.nearPlane);
	}
	
	return calcOrthoProjRevZ(p.orthographic.width,
		p.orthographic.height, p.orthographic.depth);
}
float CameraComponent::getNearPlane() const noexcept
{
	if (type == ProjectionType::Perspective)
		return p.perspective.nearPlane;
	return p.orthographic.depth.x;
}

//**********************************************************************************************************************
const string& CameraSystem::getComponentName() const
{
	static const string name = "Camera";
	return name;
}
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

//**********************************************************************************************************************
void CameraSystem::serialize(ISerializer& serializer, ID<Component> component)
{
	auto cameraComponent = components.get(ID<CameraComponent>(component));
	if (cameraComponent->type == ProjectionType::Perspective)
	{
		serializer.write("type", "perspective");
		serializer.write("fieldOfView", cameraComponent->p.perspective.fieldOfView);
		serializer.write("aspectRatio", cameraComponent->p.perspective.aspectRatio);
		serializer.write("nearPlane", cameraComponent->p.perspective.nearPlane);
	}
	else
	{
		serializer.write("type", "orthographic");
		serializer.write("width", cameraComponent->p.orthographic.width);
		serializer.write("height", cameraComponent->p.orthographic.height);
		serializer.write("depth", cameraComponent->p.orthographic.depth);
	}
}
void CameraSystem::deserialize(IDeserializer& deserializer, ID<Component> component)
{
	auto cameraComponent = components.get(ID<CameraComponent>(component));

	string type;
	deserializer.read("type", type);

	if (type == "perspective")
	{
		deserializer.read("fieldOfView", cameraComponent->p.perspective.fieldOfView);
		deserializer.read("aspectRatio", cameraComponent->p.perspective.aspectRatio);
		deserializer.read("nearPlane", cameraComponent->p.perspective.nearPlane);
	}
	else if (type == "orthographic")
	{
		deserializer.read("width", cameraComponent->p.orthographic.width);
		deserializer.read("height", cameraComponent->p.orthographic.height);
		deserializer.read("depth", cameraComponent->p.orthographic.depth);
	}
}
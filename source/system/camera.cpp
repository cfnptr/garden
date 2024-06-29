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
ID<Component> CameraSystem::createComponent(ID<Entity> entity)
{
	return ID<Component>(components.create());
}
void CameraSystem::destroyComponent(ID<Component> instance)
{
	components.destroy(ID<CameraComponent>(instance));
}
void CameraSystem::copyComponent(ID<Component> source, ID<Component> destination)
{
	const auto sourceView = components.get(ID<CameraComponent>(source));
	auto destinationView = components.get(ID<CameraComponent>(destination));
	destinationView->p = sourceView->p;
	destinationView->type = sourceView->type;
}

//**********************************************************************************************************************
void CameraSystem::serialize(ISerializer& serializer, ID<Entity> entity, View<Component> component)
{
	auto cameraComponent = View<CameraComponent>(component);
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
void CameraSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto cameraComponent = View<CameraComponent>(component);

	string type;
	deserializer.read("type", type);

	if (type == "perspective")
	{
		cameraComponent->type = ProjectionType::Perspective;
		deserializer.read("fieldOfView", cameraComponent->p.perspective.fieldOfView);
		deserializer.read("aspectRatio", cameraComponent->p.perspective.aspectRatio);
		deserializer.read("nearPlane", cameraComponent->p.perspective.nearPlane);
	}
	else if (type == "orthographic")
	{
		cameraComponent->type = ProjectionType::Orthographic;
		deserializer.read("width", cameraComponent->p.orthographic.width);
		deserializer.read("height", cameraComponent->p.orthographic.height);
		deserializer.read("depth", cameraComponent->p.orthographic.depth);
	}
}

//**********************************************************************************************************************
void CameraSystem::serializeAnimation(ISerializer& serializer, ID<AnimationFrame> frame)
{
	auto cameraFrame = animationFrames.get(ID<CameraFrame>(frame));
	if (cameraFrame->f.base.type == ProjectionType::Perspective)
	{
		serializer.write("type", "perspective");
		if (cameraFrame->f.perspective.animateFieldOfView)
			serializer.write("fieldOfView", cameraFrame->c.perspective.fieldOfView);
		if (cameraFrame->f.perspective.animateAspectRatio)
			serializer.write("aspectRatio", cameraFrame->c.perspective.aspectRatio);
		if (cameraFrame->f.perspective.animateNearPlane)
			serializer.write("nearPlane", cameraFrame->c.perspective.nearPlane);
	}
	else
	{
		serializer.write("type", "orthographic");
		if (cameraFrame->f.orthographic.animateWidth)
			serializer.write("width", cameraFrame->c.orthographic.width);
		if (cameraFrame->f.orthographic.animateHeight)
			serializer.write("height", cameraFrame->c.orthographic.height);
		if (cameraFrame->f.orthographic.animateDepth)
			serializer.write("depth", cameraFrame->c.orthographic.depth);
	}
}
ID<AnimationFrame> CameraSystem::deserializeAnimation(IDeserializer& deserializer)
{
	string type;
	deserializer.read("type", type);

	CameraFrame cameraFrame;
	if (type == "perspective")
	{
		cameraFrame.f.perspective.type = ProjectionType::Perspective;
		cameraFrame.f.perspective.animateFieldOfView =
			deserializer.read("fieldOfView", cameraFrame.c.perspective.fieldOfView);
		cameraFrame.f.perspective.animateAspectRatio =
			deserializer.read("aspectRatio", cameraFrame.c.perspective.aspectRatio);
		cameraFrame.f.perspective.animateNearPlane =
			deserializer.read("nearPlane", cameraFrame.c.perspective.nearPlane);
	}
	else if (type == "orthographic")
	{
		cameraFrame.f.orthographic.type = ProjectionType::Orthographic;
		cameraFrame.f.orthographic.animateWidth =
			deserializer.read("width", cameraFrame.c.orthographic.width);
		cameraFrame.f.orthographic.animateHeight =
			deserializer.read("height", cameraFrame.c.orthographic.height);
		cameraFrame.f.orthographic.animateDepth =
			deserializer.read("depth", cameraFrame.c.orthographic.depth);
	}

	if (cameraFrame.f.base.animate0 || cameraFrame.f.base.animate1 || cameraFrame.f.base.animate2)
	{
		auto frame = animationFrames.create();
		auto frameView = animationFrames.get(frame);
		**frameView = cameraFrame;
		return ID<AnimationFrame>(frame);
	}

	return {};
}
void CameraSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<CameraFrame>(frame));
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
View<Component> CameraSystem::getComponent(ID<Component> instance)
{
	return View<Component>(components.get(ID<CameraComponent>(instance)));
}
void CameraSystem::disposeComponents()
{
	components.dispose();
	animationFrames.dispose();
}
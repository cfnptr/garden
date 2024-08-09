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
void CameraSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<CameraComponent>(source);
	auto destinationView = View<CameraComponent>(destination);
	destinationView->p = sourceView->p;
	destinationView->type = sourceView->type;
}
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

//**********************************************************************************************************************
void CameraSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	auto componentView = View<CameraComponent>(component);
	if (componentView->type == ProjectionType::Perspective)
	{
		serializer.write("projection", string_view("Perspective"));
		serializer.write("fieldOfView", componentView->p.perspective.fieldOfView);
		serializer.write("aspectRatio", componentView->p.perspective.aspectRatio);
		serializer.write("nearPlane", componentView->p.perspective.nearPlane);
	}
	else
	{
		serializer.write("projection", string_view("Orthographic"));
		serializer.write("width", componentView->p.orthographic.width);
		serializer.write("height", componentView->p.orthographic.height);
		serializer.write("depth", componentView->p.orthographic.depth);
	}
}
void CameraSystem::deserialize(IDeserializer& deserializer, ID<Entity> entity, View<Component> component)
{
	auto componentView = View<CameraComponent>(component);

	string type;
	deserializer.read("projection", type);

	if (type == "Perspective")
	{
		componentView->type = ProjectionType::Perspective;
		deserializer.read("fieldOfView", componentView->p.perspective.fieldOfView);
		deserializer.read("aspectRatio", componentView->p.perspective.aspectRatio);
		deserializer.read("nearPlane", componentView->p.perspective.nearPlane);
	}
	else if (type == "Orthographic")
	{
		componentView->type = ProjectionType::Orthographic;
		deserializer.read("width", componentView->p.orthographic.width);
		deserializer.read("height", componentView->p.orthographic.height);
		deserializer.read("depth", componentView->p.orthographic.depth);
	}
}

//**********************************************************************************************************************
void CameraSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	auto frameView = View<CameraFrame>(frame);
	if (frameView->f.base.type == ProjectionType::Perspective)
	{
		serializer.write("projection", string_view("Perspective"));
		if (frameView->f.perspective.animateFieldOfView)
			serializer.write("fieldOfView", frameView->c.perspective.fieldOfView);
		if (frameView->f.perspective.animateAspectRatio)
			serializer.write("aspectRatio", frameView->c.perspective.aspectRatio);
		if (frameView->f.perspective.animateNearPlane)
			serializer.write("nearPlane", frameView->c.perspective.nearPlane);
	}
	else
	{
		serializer.write("projection", string_view("Orthographic"));
		if (frameView->f.orthographic.animateWidth)
			serializer.write("width", frameView->c.orthographic.width);
		if (frameView->f.orthographic.animateHeight)
			serializer.write("height", frameView->c.orthographic.height);
		if (frameView->f.orthographic.animateDepth)
			serializer.write("depth", frameView->c.orthographic.depth);
	}
}
ID<AnimationFrame> CameraSystem::deserializeAnimation(IDeserializer& deserializer)
{
	string type;
	deserializer.read("projection", type);

	CameraFrame frame;
	if (type == "perspective")
	{
		frame.f.perspective.type = ProjectionType::Perspective;
		frame.f.perspective.animateFieldOfView = deserializer.read("fieldOfView", frame.c.perspective.fieldOfView);
		frame.f.perspective.animateAspectRatio = deserializer.read("aspectRatio", frame.c.perspective.aspectRatio);
		frame.f.perspective.animateNearPlane = deserializer.read("nearPlane", frame.c.perspective.nearPlane);
	}
	else if (type == "orthographic")
	{
		frame.f.orthographic.type = ProjectionType::Orthographic;
		frame.f.orthographic.animateWidth = deserializer.read("width", frame.c.orthographic.width);
		frame.f.orthographic.animateHeight = deserializer.read("height", frame.c.orthographic.height);
		frame.f.orthographic.animateDepth = deserializer.read("depth", frame.c.orthographic.depth);
	}

	if (frame.f.base.animate0 || frame.f.base.animate1 || frame.f.base.animate2)
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}
View<AnimationFrame> CameraSystem::getAnimation(ID<AnimationFrame> frame)
{
	return View<AnimationFrame>(animationFrames.get(ID<CameraFrame>(frame)));
}

//**********************************************************************************************************************
void CameraSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<CameraComponent>(component);
	auto frameA = View<CameraFrame>(a);
	auto frameB = View<CameraFrame>(b);

	if (frameA->f.base.type == ProjectionType::Perspective)
	{
		if (frameA->f.perspective.animateFieldOfView)
		{
			componentView->p.perspective.fieldOfView = lerp(
				frameA->c.perspective.fieldOfView, frameB->c.perspective.fieldOfView, t);
		}
		if (frameA->f.perspective.animateAspectRatio)
		{
			componentView->p.perspective.aspectRatio = lerp(
				frameA->c.perspective.aspectRatio, frameB->c.perspective.aspectRatio, t);
		}
		if (frameA->f.perspective.animateNearPlane)
		{
			componentView->p.perspective.nearPlane = lerp(
				frameA->c.perspective.nearPlane, frameB->c.perspective.nearPlane, t);
		}
	}
	else
	{
		if (frameA->f.orthographic.animateWidth)
		{
			componentView->p.orthographic.width = lerp(
				frameA->c.orthographic.width, frameB->c.orthographic.width, t);
		}
		if (frameA->f.orthographic.animateHeight)
		{
			componentView->p.orthographic.height = lerp(
				frameA->c.orthographic.height, frameB->c.orthographic.height, t);
		}
		if (frameA->f.orthographic.animateDepth)
		{
			componentView->p.orthographic.depth = lerp(
				frameA->c.orthographic.depth, frameB->c.orthographic.depth, t);
		}
	}
}
void CameraSystem::destroyAnimation(ID<AnimationFrame> frame)
{
	animationFrames.destroy(ID<CameraFrame>(frame));
}
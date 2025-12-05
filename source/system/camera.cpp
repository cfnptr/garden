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

#include "garden/system/camera.hpp"
#include "math/matrix/projection.hpp"

using namespace garden;

f32x4x4 CameraComponent::calcProjection() const noexcept
{
	if (type == ProjectionType::Perspective)
	{
		return (f32x4x4)calcPerspProjInfRevZ(p.perspective.fieldOfView,
			p.perspective.aspectRatio, p.perspective.nearPlane);
	}
	
	return (f32x4x4)calcOrthoProjRevZ(p.orthographic.width,
		p.orthographic.height, p.orthographic.depth);
}
float CameraComponent::getNearPlane() const noexcept
{
	if (type == ProjectionType::Perspective)
		return p.perspective.nearPlane;
	return p.orthographic.depth.x;
}

//**********************************************************************************************************************
CameraSystem::CameraSystem(bool setSingleton) : Singleton(setSingleton)
{
	auto manager = Manager::Instance::get();
	manager->addGroupSystem<ISerializable>(this);
	manager->addGroupSystem<IAnimatable>(this);
}
CameraSystem::~CameraSystem()
{
	if (Manager::Instance::get()->isRunning)
	{
		auto manager = Manager::Instance::get();
		manager->removeGroupSystem<ISerializable>(this);
		manager->removeGroupSystem<IAnimatable>(this);
	}
	unsetSingleton();
}

string_view CameraSystem::getComponentName() const
{
	return "Camera";
}

//**********************************************************************************************************************
void CameraSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto componentView = View<CameraComponent>(component);
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
void CameraSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto componentView = View<CameraComponent>(component);
	if (deserializer.read("projection", valueStringCache))
	{
		if (valueStringCache == "Perspective")
		{
			componentView->type = ProjectionType::Perspective;
			deserializer.read("fieldOfView", componentView->p.perspective.fieldOfView);
			deserializer.read("aspectRatio", componentView->p.perspective.aspectRatio);
			deserializer.read("nearPlane", componentView->p.perspective.nearPlane);
		}
		else if (valueStringCache == "Orthographic")
		{
			componentView->type = ProjectionType::Orthographic;
			deserializer.read("width", componentView->p.orthographic.width);
			deserializer.read("height", componentView->p.orthographic.height);
			deserializer.read("depth", componentView->p.orthographic.depth);
		}
	}
}

//**********************************************************************************************************************
void CameraSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto frameView = View<CameraFrame>(frame);
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
void CameraSystem::deserializeAnimation(IDeserializer& deserializer, View<AnimationFrame> frame)
{
	auto frameView = View<CameraFrame>(frame);
	string type; deserializer.read("projection", type);

	if (type == "Perspective")
	{
		frameView->f.perspective.type = ProjectionType::Perspective;
		frameView->f.perspective.animateFieldOfView = deserializer.read(
			"fieldOfView", frameView->c.perspective.fieldOfView);
		frameView->f.perspective.animateAspectRatio = deserializer.read(
			"aspectRatio", frameView->c.perspective.aspectRatio);
		frameView->f.perspective.animateNearPlane = deserializer.read(
			"nearPlane", frameView->c.perspective.nearPlane);
	}
	else if (type == "Orthographic")
	{
		frameView->f.orthographic.type = ProjectionType::Orthographic;
		frameView->f.orthographic.animateWidth = deserializer.read("width", frameView->c.orthographic.width);
		frameView->f.orthographic.animateHeight = deserializer.read("height", frameView->c.orthographic.height);
		frameView->f.orthographic.animateDepth = deserializer.read("depth", frameView->c.orthographic.depth);
	}
}

//**********************************************************************************************************************
void CameraSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto componentView = View<CameraComponent>(component);
	const auto frameA = View<CameraFrame>(a);
	const auto frameB = View<CameraFrame>(b);

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
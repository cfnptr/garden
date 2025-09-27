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

void CameraSystem::resetComponent(View<Component> component, bool full)
{
	if (full)
	{
		auto cameraView = View<CameraComponent>(component);
		cameraView->p = {};
		cameraView->type = ProjectionType::Perspective;
	}
}
void CameraSystem::copyComponent(View<Component> source, View<Component> destination)
{
	const auto sourceView = View<CameraComponent>(source);
	auto destinationView = View<CameraComponent>(destination);
	destinationView->p = sourceView->p;
	destinationView->type = sourceView->type;
}
string_view CameraSystem::getComponentName() const
{
	return "Camera";
}

//**********************************************************************************************************************
void CameraSystem::serialize(ISerializer& serializer, const View<Component> component)
{
	const auto cameraView = View<CameraComponent>(component);
	if (cameraView->type == ProjectionType::Perspective)
	{
		serializer.write("projection", string_view("Perspective"));
		serializer.write("fieldOfView", cameraView->p.perspective.fieldOfView);
		serializer.write("aspectRatio", cameraView->p.perspective.aspectRatio);
		serializer.write("nearPlane", cameraView->p.perspective.nearPlane);
	}
	else
	{
		serializer.write("projection", string_view("Orthographic"));
		serializer.write("width", cameraView->p.orthographic.width);
		serializer.write("height", cameraView->p.orthographic.height);
		serializer.write("depth", cameraView->p.orthographic.depth);
	}
}
void CameraSystem::deserialize(IDeserializer& deserializer, View<Component> component)
{
	auto cameraView = View<CameraComponent>(component);

	string type; deserializer.read("projection", type);
	if (type == "Perspective")
	{
		cameraView->type = ProjectionType::Perspective;
		deserializer.read("fieldOfView", cameraView->p.perspective.fieldOfView);
		deserializer.read("aspectRatio", cameraView->p.perspective.aspectRatio);
		deserializer.read("nearPlane", cameraView->p.perspective.nearPlane);
	}
	else if (type == "Orthographic")
	{
		cameraView->type = ProjectionType::Orthographic;
		deserializer.read("width", cameraView->p.orthographic.width);
		deserializer.read("height", cameraView->p.orthographic.height);
		deserializer.read("depth", cameraView->p.orthographic.depth);
	}
}

//**********************************************************************************************************************
void CameraSystem::serializeAnimation(ISerializer& serializer, View<AnimationFrame> frame)
{
	const auto cameraFrameView = View<CameraFrame>(frame);
	if (cameraFrameView->f.base.type == ProjectionType::Perspective)
	{
		serializer.write("projection", string_view("Perspective"));
		if (cameraFrameView->f.perspective.animateFieldOfView)
			serializer.write("fieldOfView", cameraFrameView->c.perspective.fieldOfView);
		if (cameraFrameView->f.perspective.animateAspectRatio)
			serializer.write("aspectRatio", cameraFrameView->c.perspective.aspectRatio);
		if (cameraFrameView->f.perspective.animateNearPlane)
			serializer.write("nearPlane", cameraFrameView->c.perspective.nearPlane);
	}
	else
	{
		serializer.write("projection", string_view("Orthographic"));
		if (cameraFrameView->f.orthographic.animateWidth)
			serializer.write("width", cameraFrameView->c.orthographic.width);
		if (cameraFrameView->f.orthographic.animateHeight)
			serializer.write("height", cameraFrameView->c.orthographic.height);
		if (cameraFrameView->f.orthographic.animateDepth)
			serializer.write("depth", cameraFrameView->c.orthographic.depth);
	}
}
ID<AnimationFrame> CameraSystem::deserializeAnimation(IDeserializer& deserializer)
{
	string type; deserializer.read("projection", type);

	CameraFrame frame;
	if (type == "Perspective")
	{
		frame.f.perspective.type = ProjectionType::Perspective;
		frame.f.perspective.animateFieldOfView = deserializer.read("fieldOfView", frame.c.perspective.fieldOfView);
		frame.f.perspective.animateAspectRatio = deserializer.read("aspectRatio", frame.c.perspective.aspectRatio);
		frame.f.perspective.animateNearPlane = deserializer.read("nearPlane", frame.c.perspective.nearPlane);
	}
	else if (type == "Orthographic")
	{
		frame.f.orthographic.type = ProjectionType::Orthographic;
		frame.f.orthographic.animateWidth = deserializer.read("width", frame.c.orthographic.width);
		frame.f.orthographic.animateHeight = deserializer.read("height", frame.c.orthographic.height);
		frame.f.orthographic.animateDepth = deserializer.read("depth", frame.c.orthographic.depth);
	}

	if (frame.hasAnimation())
		return ID<AnimationFrame>(animationFrames.create(frame));
	return {};
}

//**********************************************************************************************************************
void CameraSystem::animateAsync(View<Component> component, View<AnimationFrame> a, View<AnimationFrame> b, float t)
{
	auto cameraView = View<CameraComponent>(component);
	const auto frameA = View<CameraFrame>(a);
	const auto frameB = View<CameraFrame>(b);

	if (frameA->f.base.type == ProjectionType::Perspective)
	{
		if (frameA->f.perspective.animateFieldOfView)
		{
			cameraView->p.perspective.fieldOfView = lerp(
				frameA->c.perspective.fieldOfView, frameB->c.perspective.fieldOfView, t);
		}
		if (frameA->f.perspective.animateAspectRatio)
		{
			cameraView->p.perspective.aspectRatio = lerp(
				frameA->c.perspective.aspectRatio, frameB->c.perspective.aspectRatio, t);
		}
		if (frameA->f.perspective.animateNearPlane)
		{
			cameraView->p.perspective.nearPlane = lerp(
				frameA->c.perspective.nearPlane, frameB->c.perspective.nearPlane, t);
		}
	}
	else
	{
		if (frameA->f.orthographic.animateWidth)
		{
			cameraView->p.orthographic.width = lerp(
				frameA->c.orthographic.width, frameB->c.orthographic.width, t);
		}
		if (frameA->f.orthographic.animateHeight)
		{
			cameraView->p.orthographic.height = lerp(
				frameA->c.orthographic.height, frameB->c.orthographic.height, t);
		}
		if (frameA->f.orthographic.animateDepth)
		{
			cameraView->p.orthographic.depth = lerp(
				frameA->c.orthographic.depth, frameB->c.orthographic.depth, t);
		}
	}
}
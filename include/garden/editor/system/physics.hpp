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

#pragma once
#include "garden/system/render/editor.hpp"

#if GARDEN_EDITOR
#include "garden/system/character.hpp"

namespace garden
{

class PhysicsEditorSystem final : public System
{
public:
	struct RigidbodyCache final
	{
		f32x4 centerOfMass = f32x4::zero;
		f32x4 shapePosition = f32x4::zero;
		f32x4 halfExtent = f32x4(0.5f);
		f32x4 thisConstraintPoint = f32x4::zero;
		f32x4 otherConstraintPoint = f32x4::zero;
		float convexRadius = 0.05f;
		float halfHeight = 0.875f;
		float shapeRadius = 0.3f;
		float density = 1000.0f;
		int collisionLayer = -1;
		ID<Entity> constraintTarget = {};
		bool autoConstraintPoints = true;
		bool isSensor = false;
		MotionType motionType = {};
		ConstraintType constraintType = {};
		AllowedDOF allowedDOF = {};
	};
	struct CharacterCache final
	{
		f32x4 centerOfMass = f32x4::zero;
		f32x4 shapePosition = f32x4::zero;
		f32x4 shapeSize = f32x4(0.3f, 1.75f, 0.3f);
		float convexRadius = 0.05f;
		float shapeHeight = 1.75f;
		float shapeRadius = 0.3f;
	};
private:
	f32x4 oldRigidbodyEulerAngles = f32x4::zero;
	f32x4 newRigidbodyEulerAngles = f32x4::zero;
	quat oldRigidbodyRotation = quat::identity;
	f32x4 oldCharacterEulerAngles = f32x4::zero;
	f32x4 newCharacterEulerAngles = f32x4::zero;
	quat oldCharacterRotation = quat::identity;
	void* debugRenderer = nullptr;
	RigidbodyCache rigidbodyCache = {};
	CharacterCache characterCache = {};
	ID<Entity> rigidbodySelectedEntity = {};
	ID<Entity> characterSelectedEntity = {};
	bool showWindow = false;

	PhysicsEditorSystem();
	~PhysicsEditorSystem() final;

	void init();
	void deinit();
	void preMetaLdrRender();
	void metaLdrRender();
	void editorBarTool();

	void onRigidbodyInspector(ID<Entity> entity, bool isOpened);
	void onCharacterInspector(ID<Entity> entity, bool isOpened);

	friend class ecsm::Manager;
public:
	bool drawShapes = false;
	bool drawBoundingBox = false;
	bool drawCenterOfMass = false;
	bool drawConstraints = false;
	bool drawConstraintLimits = false;
	bool drawConstraintRefFrame = false;
	float rigidbodyInspectorPriority = 0.8f;
	float characterInspectorPriority = 0.75f;
	Color rigidbodyAabbColor = Color::green;
	Color characterAabbColor = Color("00FF7FFF");
};

} // namespace garden
#endif
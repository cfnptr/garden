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
		float3 centerOfMass = float3(0.0f);
		float3 shapePosition = float3(0.0f);
		float3 halfExtent = float3(0.5f);
		float convexRadius = 0.05f;
		float density = 1000.0f;
		int collisionLayer = -1;
		float3 thisConstraintPoint = float3(0.0f);
		float3 otherConstraintPoint = float3(0.0f);
		ID<Entity> constraintTarget = {};
		bool autoConstraintPoints = true;
		bool isSensor = false;
		MotionType motionType = {};
		ConstraintType constraintType = {};
		AllowedDOF allowedDOF = {};
	};
	struct CharacterCache final
	{
		float3 centerOfMass = float3(0.0f);
		float3 shapePosition = float3(0.0f);
		float3 shapeSize = float3(0.5f, 1.75f, 0.5f);
		float convexRadius = 0.05f;
	};
private:
	void* debugRenderer = nullptr;
	RigidbodyCache rigidbodyCache = {};
	CharacterCache characterCache = {};
	float3 oldRigidbodyEulerAngles = float3(0.0f);
	float3 newRigidbodyEulerAngles = float3(0.0f);
	quat oldRigidbodyRotation = quat::identity;
	float3 oldCharacterEulerAngles = float3(0.0f);
	float3 newCharacterEulerAngles = float3(0.0f);
	quat oldCharacterRotation = quat::identity;
	ID<Entity> rigidbodySelectedEntity = {};
	ID<Entity> characterSelectedEntity = {};
	bool showWindow = false;

	PhysicsEditorSystem();
	~PhysicsEditorSystem() final;

	void init();
	void deinit();
	void editorRender();
	void editorBarTool();

	void onRigidbodyInspector(ID<Entity> entity, bool isOpened);
	void onCharacterInspector(ID<Entity> entity, bool isOpened);

	friend class ecsm::Manager;
public:
	bool drawBodies = false;
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
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

#include "garden/editor/system/physics.hpp"

#if GARDEN_EDITOR
using namespace garden;

//--------------------------------------------------------------------------------------------------
PhysicsEditor::PhysicsEditor(PhysicsSystem* system)
{
	EditorRenderSystem::getInstance()->registerEntityInspector(typeid(RigidBodyComponent),
		[this](ID<Entity> entity) { onEntityInspector(entity); });
	this->system = system;
}

//--------------------------------------------------------------------------------------------------
void PhysicsEditor::onEntityInspector(ID<Entity> entity)
{
	if (ImGui::CollapsingHeader("Rigid Body"))
	{
		auto manager = getManager();
		auto rigidBodyComponent = manager->get<RigidBodyComponent>(entity);

		auto isStatic = rigidBodyComponent->isStatic();
		if (ImGui::Checkbox("Static", &isStatic))
		{
			rigidBodyComponent->setStatic(isStatic);
			if (!isStatic)
			{
				rigidBodyComponent->setLinearDamping(0.025f);
				rigidBodyComponent->setAngularDamping(0.05f);
				rigidBodyComponent->setSleepThreshold(0.05f);
				rigidBodyComponent->setSolverIterCount(8, 2);
			}
		}

		if (!isStatic)
		{
			ImGui::SameLine();
			auto boolValue = rigidBodyComponent->isSleeping();
			if (ImGui::Checkbox("Sleeping", &boolValue))
			{
				if (boolValue)
					rigidBodyComponent->putToSleep();
				else
					rigidBodyComponent->wakeUp();
			}

			auto floatValue = rigidBodyComponent->getMass();
			if (ImGui::DragFloat("Mass", &floatValue, 0.01f, 0.0f, FLT_MAX))
				rigidBodyComponent->setMass(floatValue);

			ImGui::SeparatorText("Damping Coefficient");
			floatValue = rigidBodyComponent->getLinearDamping();
			if (ImGui::DragFloat("Linear", &floatValue, 0.01f, 0.0f, FLT_MAX))
				rigidBodyComponent->setLinearDamping(floatValue);
			floatValue = rigidBodyComponent->getAngularDamping();
			if (ImGui::DragFloat("Angular", &floatValue, 0.01f, 0.0f, FLT_MAX))
				rigidBodyComponent->setAngularDamping(floatValue);

			if (ImGui::CollapsingHeader("Advanced Settings"))
			{
				auto float3Value = rigidBodyComponent->getCenterOfMass();
				if (ImGui::DragFloat3("Center Of Mass",
					(float*)&float3Value, 0.01f, 0.0f, FLT_MAX))
				{
					rigidBodyComponent->setCenterOfMass(float3Value);
				}
				float3Value = rigidBodyComponent->getInertiaTensor();
				if (ImGui::DragFloat3("Inertia Tensor",
					(float*)&float3Value, 0.01f, 0.0f, FLT_MAX))
				{
					rigidBodyComponent->setInertiaTensor(float3Value);
				}
				if (ImGui::Button("Calculate Mass and Inertia", ImVec2(-FLT_MIN, 0.0f)))
					rigidBodyComponent->calcMassAndInertia();

				ImGui::Spacing();
				floatValue = rigidBodyComponent->getSleepThreshold();
				if (ImGui::DragFloat("Sleep Threshold", &floatValue, 0.01f, 0.0f, FLT_MAX))
					rigidBodyComponent->setSleepThreshold(floatValue);
				
				ImGui::SeparatorText("Solver Iteration Count");
				uint32 minPosition = 0, minVelocity = 0;
				rigidBodyComponent->getSolverIterCount(minPosition, minVelocity);
				int mp = minPosition, mv = minVelocity;
				ImGui::InputInt("Min Position", &mp);
				ImGui::InputInt("Min Velocity", &mv);
				if (mp > 0 && mv > 0 && mp != minPosition && mv != minVelocity)
					rigidBodyComponent->setSolverIterCount(mp, mv);

				ImGui::SeparatorText("Velocity");
				float3Value = rigidBodyComponent->getLinearVelocity();
				if (ImGui::DragFloat3("Linear",
					(float*)&float3Value, 0.01f, 0.0f, FLT_MAX))
					rigidBodyComponent->setLinearVelocity(float3Value);
				float3Value = rigidBodyComponent->getAngularVelocity();
				if (ImGui::DragFloat3("Angular",
					(float*)&float3Value, 0.01f, 0.0f, FLT_MAX))
					rigidBodyComponent->setAngularVelocity(float3Value);
			}

			if (ImGui::CollapsingHeader("Shapes"))
			{
				// TODO: show shapes.
			}
		}
	}
}
#endif
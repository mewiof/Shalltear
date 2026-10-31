#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Joints.h"
#include "Shalltear/Physics3D/PhysicsMaterial.h"
#include "Shalltear/Physics3D/Rigidbody.h"

#include "PhysicsBackend.h"
#include "PhysicsSettings.h"
#include "PhysicsWorld.h"

#if defined(SLT_WITH_EDITOR)
#include "Physics3DEditor.h"
#endif

namespace Shalltear {

	void RegisterPhysicsMaterialImporter(const void* owner);

	namespace Physics3DImpl {

		constexpr const char* PACKAGE_DISPLAY_NAME = "Physics 3D";

		constexpr const char* SETTINGS_CATEGORY_PATH = "Physics";

		// must outlive the world
		PhysicsSettings _settingsFallback;

		void StepStage(const FrameStageArgs&, void* user) {
			auto* world = static_cast<PhysicsWorld*>(user);

			world->Step(Time::GetDeltaTime());
		}

		void InterpolateStage(const FrameStageArgs&, void* user) {
			auto* world = static_cast<PhysicsWorld*>(user);
			world->Interpolate(Time::GetFixedAlpha());
		}

		void RegisterPackage(const void* owner) {
			PhysicsSettings* settings =
				Settings::DeclareCategory<PhysicsSettings>(SETTINGS_CATEGORY_PATH, SettingScope::Project, true);
			if (!settings) {
				SLT_LOG_ERROR("Physics", "couldn't register the {} settings category", SETTINGS_CATEGORY_PATH);
				settings = &_settingsFallback;
			}

			PhysicsWorld& world = GetPhysicsWorld();
			FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Late, &StepStage, &world, owner);
			FrameStages::Register(FrameStage::Update, FrameStagePosition::Late, &InterpolateStage, &world, owner);

			const bool up = world.Initialize(*settings);

			// static player drops types without these
			Rigidbody::GetReflectType();
			Collider::GetReflectType();
			BoxCollider::GetReflectType();
			SphereCollider::GetReflectType();
			CapsuleCollider::GetReflectType();
			MeshCollider::GetReflectType();
			Joint::GetReflectType();
			HingeJoint::GetReflectType();
			FixedJoint::GetReflectType();
			SpringJoint::GetReflectType();
			CharacterJoint::GetReflectType();
			PrismaticJoint::GetReflectType();
			PhysicsMaterial::GetReflectType();

			RegisterPhysicsMaterialImporter(owner);

			const i32 backendBytes = PhysicsBackend::GetAllocatedBytes();
			SLT_LOG_INFO("Physics", "{} up: backend {}, world {}, {} island(s), {} worker(s), backend bytes {}",
						 PACKAGE_DISPLAY_NAME, PhysicsBackend::Name(),
						 up ? "created" : "UNAVAILABLE", world.GetIslandCount(), world.GetWorkerCount(), backendBytes);
		}

		// before DLL detach (joining workers under loader lock deadlocks)
		void ShutdownPackage() {
#if defined(SLT_WITH_EDITOR)
			Physics3DEditor::ReleaseHeldBodies();
#endif
			GetPhysicsWorld().Shutdown();
		}

	} // namespace Physics3DImpl

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(Physics3D,
									&Shalltear::Physics3DImpl::RegisterPackage,
									&Shalltear::Physics3DImpl::ShutdownPackage)

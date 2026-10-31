#include "TestsPCH.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(Physics3D)
#endif

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "Physics3D";
	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	constexpr u32 STEPS_PER_RUN = 240;

	const FieldInfo* FindField(const TypeInfo* type, std::string_view fieldName) {
		if (!type) {
			return nullptr;
		}
		StringHash wanted(fieldName);
		for (u32 index = 0; index < type->fieldCount; ++index) {
			if (type->fields[index].name == wanted) {
				return &type->fields[index];
			}
		}
		return nullptr;
	}

	template <typename T>
	bool WriteField(const TypeInfo* type, void* instance, std::string_view fieldName, const T& value) {
		const FieldInfo* field = FindField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		*static_cast<T*>(field->addrFn(instance)) = value;
		return true;
	}

	class Physics3DDeterminismFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		const TypeInfo* _rigidbodyType = nullptr;
		const TypeInfo* _boxType = nullptr;
		const TypeInfo* _hingeType = nullptr;
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_determinism_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			ASSERT_TRUE(StandUp());

			_rigidbodyType = TypeRegistry::FindType(StringHash("Rigidbody"));
			_boxType = TypeRegistry::FindType(StringHash("BoxCollider"));
			_hingeType = TypeRegistry::FindType(StringHash("HingeJoint"));
			ASSERT_NE(_rigidbodyType, nullptr);
			ASSERT_NE(_boxType, nullptr);
			ASSERT_NE(_hingeType, nullptr);
		}

		void TearDown() override {
			SceneSystem::Terminate();
#ifndef SHALLTEAR_SHARED
			FrameStages::UnregisterOwnedBy(&_token);
#endif
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StandUp() {
#ifdef SHALLTEAR_SHARED
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			return ModuleSystem::IsLoaded(_module);
#else
			SLT_CALL_LINKED_PACKAGE(Physics3D, &_token);
			return true;
#endif
		}

		Handle<SceneObject> MakeFloor(Handle<Scene> scene) {
			Handle<SceneObject> floor = SceneSystem::CreateObject("floor", scene);
			floor->SetWorldPosition(Vec3{0.0f, -0.5f, 0.0f});
			floor->SetLocalScale(Vec3{40.0f, 1.0f, 40.0f});
			floor->AddComponent(_boxType);
			return floor;
		}

		Handle<Scene> BuildTopplingStack() {
			Handle<Scene> scene = SceneSystem::CreateScene("stack");
			if (!scene.IsValid()) {
				return {};
			}
			MakeFloor(scene);

			for (u32 index = 0; index < 12; ++index) {
				Handle<SceneObject> box = SceneSystem::CreateObject("box", scene);
				const f32 f = static_cast<f32>(index);
				box->SetWorldPosition(Vec3{0.28f * f, 0.6f + 1.02f * f, 0.11f * f});
				box->AddComponent(_boxType);
				Handle<Component> body = box->AddComponent(_rigidbodyType);
				WriteField(_rigidbodyType, body.Get(), "_sleepThreshold", 0.0f);
			}
			return scene;
		}

		Handle<Scene> BuildHangingChain() {
			Handle<Scene> scene = SceneSystem::CreateScene("chain");
			if (!scene.IsValid()) {
				return {};
			}
			MakeFloor(scene);

			for (u32 index = 0; index < 6; ++index) {
				Handle<SceneObject> link = SceneSystem::CreateObject("link", scene);
				link->SetWorldPosition(Vec3{0.4f * static_cast<f32>(index), 6.0f, 0.0f});
				link->AddComponent(_boxType);
				Handle<Component> body = link->AddComponent(_rigidbodyType);
				WriteField(_rigidbodyType, body.Get(), "_sleepThreshold", 0.0f);
				link->AddComponent(_hingeType);
			}

			Handle<SceneObject> striker = SceneSystem::CreateObject("striker", scene);
			striker->SetWorldPosition(Vec3{1.0f, 11.0f, 0.05f});
			striker->AddComponent(_boxType);
			Handle<Component> body = striker->AddComponent(_rigidbodyType);
			WriteField(_rigidbodyType, body.Get(), "_sleepThreshold", 0.0f);
			return scene;
		}

		static void RunFixedSteps(u32 count) {
			FrameTime clockState;
			clockState.fixedDeltaTime = FIXED_STEP;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(FIXED_STEP);

			FrameStageArgs args;
			for (u32 index = 0; index < count; ++index) {
				FrameStages::Run(FrameStage::FixedUpdate, args);
			}
		}

		static str Signature(Handle<Scene> scene) {
			struct Walk {
				str* out;
				static void Visit(SceneObject& node, void* user) {
					Walk& walk = *static_cast<Walk*>(user);
					const Vec3 p = node.GetWorldPosition();
					const Quat q = node.GetWorldRotation();
					for (f32 channel : {p.x, p.y, p.z, q.x, q.y, q.z, q.w}) {
						*walk.out += std::format("{:08x} ", std::bit_cast<u32>(channel));
					}
					*walk.out += '\n';
				}
			};
			str out;
			Walk walk{&out};
			if (Scene* raw = scene.Get()) {
				raw->ForEachObject(&Walk::Visit, &walk);
			}
			return out;
		}

		void ExpectThreeRunsAgree(Handle<Scene> authored) {
			ASSERT_TRUE(authored.IsValid());

			Buffer snapshot;
			ASSERT_TRUE(SceneSystem::SaveSceneToMemory(authored, snapshot));
			const str authoredPose = Signature(authored);

			SceneSystem::UnloadAllScenes();
			SceneSystem::EndFrame();

			str first;
			for (u32 run = 0; run < 3; ++run) {
				Handle<Scene> scene = SceneSystem::LoadSceneFromMemory(snapshot);
				ASSERT_TRUE(scene.IsValid()) << "run " << run;

				RunFixedSteps(STEPS_PER_RUN);
				str signature = Signature(scene);

				SceneSystem::UnloadAllScenes();
				SceneSystem::EndFrame();

				if (run == 0) {
					first = std::move(signature);
					ASSERT_NE(first, authoredPose);
					continue;
				}
				EXPECT_EQ(signature, first) << "run " << run;
			}
		}
	};

} // anonymous namespace

TEST_F(Physics3DDeterminismFixture, StackTopplesSameWayEveryRun) {
	ExpectThreeRunsAgree(BuildTopplingStack());
}

TEST_F(Physics3DDeterminismFixture, JointedChainSwingsSameWayEveryRun) {
	ExpectThreeRunsAgree(BuildHangingChain());
}

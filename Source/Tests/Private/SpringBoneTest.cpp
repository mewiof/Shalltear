#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Animation/SpringBone.h"

using namespace Shalltear;

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	constexpr f32 SEGMENT = 0.2f;
	constexpr u32 TAILS = 3;

	class SpringBoneFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_springbone_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);
			_scene = SceneSystem::CreateScene("springs");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		SkeletonAsset* MakeChain() {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			skeleton->boneNames.push_back("Root");
			skeleton->parents.push_back(-1);
			skeleton->restPose.push_back(BoneTransform{});
			for (u32 i = 0; i < TAILS; ++i) {
				skeleton->boneNames.push_back("Tail" + std::to_string(i + 1));
				skeleton->parents.push_back(static_cast<i16>(i));
				BoneTransform segment;
				segment.position = Vec3(SEGMENT, 0.0f, 0.0f);
				skeleton->restPose.push_back(segment);
			}
			_owned.push_back(skeleton);
			return skeleton;
		}

		Handle<SpringBone> MakeTail(SkeletonAsset* skeleton, const char* name, const Vec3& at = Vec3{}) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetWorldPosition(at);
			Handle<Skeleton> rig = object->AddComponent<Skeleton>();
			rig->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			Handle<SpringBone> spring = object->AddComponent<SpringBone>();
			spring->SetRootBone("Root");
			spring->SetMaxAngleDeg(180.0f);
			return spring;
		}

		static Vec3 BoneWorld(Handle<SpringBone> spring, u32 bone) {
			SceneObject* object = spring->GetOwner().Get();
			Skeleton* rig = object->GetComponent<Skeleton>().Get();
			rig->UpdatePose();
			return (object->GetWorldMatrix() * rig->GetModelPose()[bone]).TransformPoint(Vec3{});
		}

		Handle<SpringBone> MakeObjectTail(const char* name, u32 links = TAILS) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			Handle<SceneObject> parent = object;
			for (u32 i = 0; i < links; ++i) {
				Handle<SceneObject> link = SceneSystem::CreateObject("Link", _scene, parent);
				link->SetLocalPosition(Vec3(SEGMENT, 0.0f, 0.0f));
				parent = link;
			}
			Handle<SpringBone> spring = object->AddComponent<SpringBone>();
			spring->SetMaxAngleDeg(180.0f);
			return spring;
		}

		static Handle<SceneObject> Link(Handle<SpringBone> spring, u32 link) {
			Handle<SceneObject> object = spring->GetOwner();
			for (u32 i = 0; i < link; ++i) {
				object = object->GetChild(0);
			}
			return object;
		}

		static void Run(Handle<SpringBone> spring, f32 seconds, f32 delta = 1.0f / 60.0f) {
			const u32 frames = static_cast<u32>(std::lround(seconds / delta));
			for (u32 frame = 0; frame < frames; ++frame) {
				spring->Simulate(delta);
			}
		}
	};

} // anonymous namespace

TEST_F(SpringBoneFixture, StillChainHoldsAnimatedPose) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "still");
	Run(spring, 1.0f);
	for (u32 bone = 1; bone <= TAILS; ++bone) {
		const Vec3 at = BoneWorld(spring, bone);
		EXPECT_NEAR(at.x, SEGMENT * static_cast<f32>(bone), 1e-4f) << bone;
		EXPECT_NEAR(at.y, 0.0f, 1e-4f) << bone;
		EXPECT_NEAR(at.z, 0.0f, 1e-4f) << bone;
	}
}

TEST_F(SpringBoneFixture, GravityBendsChainAndKeepsLengths) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "heavy");
	spring->SetStiffness(0.0f);
	spring->SetDamping(0.2f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	Run(spring, 2.0f);
	Vec3 previous = BoneWorld(spring, 0);
	for (u32 bone = 1; bone <= TAILS; ++bone) {
		const Vec3 at = BoneWorld(spring, bone);
		EXPECT_NEAR(Vec3::Distance(at, previous), SEGMENT, 1e-3f) << bone;
		previous = at;
	}
	EXPECT_LT(BoneWorld(spring, TAILS).y, -0.4f);
	EXPECT_LT(std::abs(BoneWorld(spring, TAILS).x), 0.15f);
}

TEST_F(SpringBoneFixture, AngleLimitHoldsFirstSegment) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "limited");
	spring->SetStiffness(0.0f);
	spring->SetDamping(0.2f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	spring->SetMaxAngleDeg(20.0f);
	Run(spring, 2.0f);
	const Vec3 first = BoneWorld(spring, 1) - BoneWorld(spring, 0);
	const f32 degrees = std::acos(Math::Clamp(first.Normalized().x, -1.0f, 1.0f)) * Math::RAD_TO_DEG;
	EXPECT_LE(degrees, 20.5f);
	EXPECT_GT(degrees, 15.0f);
}

TEST_F(SpringBoneFixture, MovedOwnerLeavesChainBehindThenSettles) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "carried");
	spring->SetStiffness(0.2f);
	spring->SetDamping(0.2f);
	Run(spring, 0.1f);
	spring->GetOwner()->SetWorldPosition(Vec3(0.0f, 0.5f, 0.0f));
	spring->Simulate(1.0f / 60.0f);
	EXPECT_LT(BoneWorld(spring, TAILS).y, 0.45f);
	EXPECT_NEAR(BoneWorld(spring, 0).y, 0.5f, 1e-5f);
	Run(spring, 3.0f);
	EXPECT_NEAR(BoneWorld(spring, TAILS).y, 0.5f, 1e-3f);
	EXPECT_NEAR(BoneWorld(spring, TAILS).x, SEGMENT * TAILS, 1e-3f);
}

TEST_F(SpringBoneFixture, SameMotionAtAnyFrameRate) {
	SkeletonAsset* skeleton = MakeChain();
	Handle<SpringBone> sixtyFour = MakeTail(skeleton, "sixty-four");
	Handle<SpringBone> twoFiftySix = MakeTail(skeleton, "two-fifty-six");
	Handle<SpringBone> thirtyTwo = MakeTail(skeleton, "thirty-two");
	for (Handle<SpringBone> spring : {sixtyFour, twoFiftySix, thirtyTwo}) {
		spring->SetStiffness(0.05f);
		spring->SetDamping(0.05f);
		spring->SetGravity(Vec3(0.0f, -4.0f, 0.0f));
		spring->SetStepRate(64.0f);
	}
	auto drive = [](Handle<SpringBone> spring, u32 framesPerSecond) {
		spring->Simulate(0.0f);
		const f32 delta = 1.0f / static_cast<f32>(framesPerSecond);
		for (u32 frame = 1; frame <= framesPerSecond * 3 / 4; ++frame) {
			const f32 time = static_cast<f32>(frame) * delta;
			spring->GetOwner()->SetWorldPosition(Vec3(0.0f, 0.3f * std::sin(2.0f * Math::PI * time), 0.0f));
			spring->Simulate(delta);
		}
	};
	drive(sixtyFour, 64);
	drive(twoFiftySix, 256);
	drive(thirtyTwo, 32);
	const Vec3 reference = BoneWorld(sixtyFour, TAILS);
	EXPECT_LT(Vec3::Distance(reference, BoneWorld(twoFiftySix, TAILS)), 1e-3f);
	EXPECT_LT(Vec3::Distance(reference, BoneWorld(thirtyTwo, TAILS)), 1e-2f);
	EXPECT_GT(Vec3::Distance(reference, Vec3(SEGMENT * TAILS, -0.3f, 0.0f)), 0.03f);
}

TEST_F(SpringBoneFixture, ColliderKeepsBonesOutside) {
	const Vec3 center(0.3f, -0.2f, 0.0f);
	auto closest = [&](Handle<SpringBone> spring) {
		f32 nearest = 1e9f;
		for (u32 frame = 0; frame < 120; ++frame) {
			spring->Simulate(1.0f / 60.0f);
			for (u32 bone = 1; bone <= TAILS; ++bone) {
				nearest = Math::Min(nearest, Vec3::Distance(BoneWorld(spring, bone), center));
			}
		}
		return nearest;
	};
	Handle<SpringBone> free = MakeTail(MakeChain(), "undraped");
	Handle<SpringBone> draped = MakeTail(MakeChain(), "draped");
	for (Handle<SpringBone> spring : {free, draped}) {
		spring->SetStiffness(0.0f);
		spring->SetDamping(0.2f);
		spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
		spring->SetRadius(0.02f);
	}
	Handle<SceneObject> ball = SceneSystem::CreateObject("ball", _scene);
	ball->SetWorldPosition(center);
	Handle<SpringBoneCollider> collider = ball->AddComponent<SpringBoneCollider>();
	collider->SetRadius(0.12f);
	draped->AddCollider(collider);
	EXPECT_LT(closest(free), 0.1f);
	EXPECT_GE(closest(draped), 0.14f - 1e-2f);
}

TEST_F(SpringBoneFixture, WeightZeroDrawsAnimatedPose) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "weighted");
	spring->SetStiffness(0.0f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	Run(spring, 1.0f);
	EXPECT_LT(BoneWorld(spring, TAILS).y, -0.1f);
	spring->SetWeight(0.0f);
	spring->Simulate(1.0f / 60.0f);
	EXPECT_NEAR(BoneWorld(spring, TAILS).y, 0.0f, 1e-5f);
	EXPECT_NEAR(BoneWorld(spring, TAILS).x, SEGMENT * TAILS, 1e-5f);
	spring->SetWeight(1.0f);
	spring->Simulate(1.0f / 60.0f);
	EXPECT_LT(BoneWorld(spring, TAILS).y, -0.1f);
}

TEST_F(SpringBoneFixture, WrittenPoseBecomesNewBase) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "animated");
	spring->SetStiffness(0.3f);
	spring->SetDamping(0.3f);
	Run(spring, 0.5f);
	Skeleton* rig = spring->GetOwner()->GetComponent<Skeleton>().Get();
	BoneTransform bent;
	bent.rotation = Quat::AngleAxis(90.0f, Vec3(0.0f, 0.0f, 1.0f));
	for (u32 frame = 0; frame < 180; ++frame) {
		rig->SetBoneLocalPose(0, bent);
		spring->Simulate(1.0f / 60.0f);
	}
	const Vec3 tip = BoneWorld(spring, TAILS);
	EXPECT_NEAR(tip.x, 0.0f, 2e-3f);
	EXPECT_NEAR(tip.y, SEGMENT * TAILS, 2e-3f);
}

TEST_F(SpringBoneFixture, LateUpdateStageSimulatesEnabledSprings) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "staged");
	spring->SetStiffness(0.0f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	FrameTime clockState;
	clockState.fixedDeltaTime = 1.0f / 50.0f;
	clockState.maximumDeltaTime = 1.0f;
	Time::FrameScope clock{clockState};
	FrameStageArgs args;
	for (u32 frame = 0; frame < 30; ++frame) {
		Time::BeginFrame(1.0f / 60.0f);
		FrameStages::Run(FrameStage::LateUpdate, args);
	}
	EXPECT_LT(BoneWorld(spring, TAILS).y, -0.05f);

	spring->SetEnabled(false);
	const Vec3 held = BoneWorld(spring, TAILS);
	for (u32 frame = 0; frame < 10; ++frame) {
		Time::BeginFrame(1.0f / 60.0f);
		FrameStages::Run(FrameStage::LateUpdate, args);
	}
	EXPECT_NEAR(Vec3::Distance(BoneWorld(spring, TAILS), held), 0.0f, 1e-6f);
}

TEST_F(SpringBoneFixture, UnknownRootLeavesPose) {
	Handle<SpringBone> spring = MakeTail(MakeChain(), "nameless");
	spring->SetRootBone("Nope");
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	Run(spring, 0.5f);
	EXPECT_NEAR(BoneWorld(spring, TAILS).y, 0.0f, 1e-6f);
}

TEST_F(SpringBoneFixture, ObjectChainMatchesBoneChain) {
	Handle<SpringBone> bones = MakeTail(MakeChain(), "bones");
	Handle<SpringBone> objects = MakeObjectTail("objects");
	for (Handle<SpringBone> spring : {bones, objects}) {
		spring->SetStiffness(0.05f);
		spring->SetDamping(0.05f);
		spring->SetGravity(Vec3(0.0f, -4.0f, 0.0f));
		spring->Simulate(0.0f);
	}
	f32 widest = 0.0f;
	f32 swung = 0.0f;
	for (u32 frame = 1; frame <= 90; ++frame) {
		const f32 time = static_cast<f32>(frame) / 60.0f;
		const Vec3 at(0.0f, 0.3f * std::sin(2.0f * Math::PI * time), 0.0f);
		for (Handle<SpringBone> spring : {bones, objects}) {
			spring->GetOwner()->SetWorldPosition(at);
			spring->Simulate(1.0f / 60.0f);
		}
		for (u32 link = 0; link <= TAILS; ++link) {
			widest = Math::Max(widest, Vec3::Distance(BoneWorld(bones, link), Link(objects, link)->GetWorldPosition()));
		}
		swung = Math::Max(swung, Vec3::Distance(Link(objects, TAILS)->GetWorldPosition(), at + Vec3(SEGMENT * TAILS, 0.0f, 0.0f)));
	}
	EXPECT_LT(widest, 1e-4f);
	EXPECT_GT(swung, 0.05f);
}

TEST_F(SpringBoneFixture, ObjectChainFollowsHierarchyChanges) {
	Handle<SpringBone> spring = MakeObjectTail("growing", 1);
	spring->SetStiffness(0.0f);
	spring->SetDamping(0.2f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	Run(spring, 2.0f);
	EXPECT_LT(Link(spring, 1)->GetWorldPosition().y, -0.19f);

	Handle<SceneObject> tip = SceneSystem::CreateObject("Tip", _scene, Link(spring, 1));
	tip->SetLocalPosition(Vec3(SEGMENT, 0.0f, 0.0f));
	Run(spring, 2.0f);
	EXPECT_LT(tip->GetWorldPosition().y, -0.39f);
	spring->SetGravity(Vec3(30.0f, 0.0f, 0.0f));
	Run(spring, 0.15f);
	const Vec3 upper = Link(spring, 1)->GetWorldPosition() - spring->GetOwner()->GetWorldPosition();
	const Vec3 lower = tip->GetWorldPosition() - Link(spring, 1)->GetWorldPosition();
	EXPECT_NEAR(lower.Length(), SEGMENT, 1e-3f);
	EXPECT_LT(Vec3::Dot(upper.Normalized(), lower.Normalized()), 0.999f);

	SceneSystem::Destroy(Link(spring, 1));
	SceneSystem::EndFrame();
	ASSERT_EQ(spring->GetOwner()->GetChildCount(), 0u);
	const Quat held = spring->GetOwner()->GetLocalRotation();
	Run(spring, 0.5f);
	EXPECT_EQ(spring->GetOwner()->GetLocalRotation(), held);
}

TEST_F(SpringBoneFixture, ObjectChainWeightZeroRestoresRotations) {
	Handle<SpringBone> spring = MakeObjectTail("restored");
	spring->SetStiffness(0.0f);
	spring->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
	Run(spring, 1.0f);
	EXPECT_LT(Link(spring, TAILS)->GetWorldPosition().y, -0.1f);
	spring->SetWeight(0.0f);
	spring->Simulate(1.0f / 60.0f);
	for (u32 link = 0; link <= TAILS; ++link) {
		EXPECT_EQ(Link(spring, link)->GetLocalRotation(), Quat::Identity()) << link;
	}
	EXPECT_NEAR(Link(spring, TAILS)->GetWorldPosition().x, SEGMENT * TAILS, 1e-5f);
}

TEST_F(SpringBoneFixture, ObjectChainTakesTurnedOwnerAsBase) {
	Handle<SpringBone> spring = MakeObjectTail("turned");
	spring->SetStiffness(0.3f);
	spring->SetDamping(0.3f);
	Run(spring, 0.5f);
	const Quat bent = Quat::AngleAxis(90.0f, Vec3(0.0f, 0.0f, 1.0f));
	for (u32 frame = 0; frame < 180; ++frame) {
		spring->GetOwner()->SetLocalRotation(bent);
		spring->Simulate(1.0f / 60.0f);
	}
	const Vec3 tip = Link(spring, TAILS)->GetWorldPosition();
	EXPECT_NEAR(tip.x, 0.0f, 2e-3f);
	EXPECT_NEAR(tip.y, SEGMENT * TAILS, 2e-3f);
}

TEST_F(SpringBoneFixture, HingeAxisKeepsSwingInPlane) {
	SkeletonAsset* skeleton = MakeChain();
	Handle<SpringBone> free = MakeTail(skeleton, "free");
	Handle<SpringBone> hinged = MakeTail(skeleton, "hinged");
	Handle<SpringBone> turned = MakeTail(skeleton, "turned");
	turned->GetOwner()->SetLocalRotation(Quat::AngleAxis(90.0f, Vec3(1.0f, 0.0f, 0.0f)));
	hinged->SetHingeAxis(SpringBoneHingeAxis::Z);
	turned->SetHingeAxis(SpringBoneHingeAxis::Z);
	for (Handle<SpringBone> spring : {free, hinged, turned}) {
		spring->SetStiffness(0.0f);
		spring->SetDamping(0.2f);
		spring->SetGravity(Vec3(0.0f, -9.81f, 6.0f));
	}
	f32 hingedOff = 0.0f;
	f32 turnedOff = 0.0f;
	for (u32 frame = 0; frame < 120; ++frame) {
		for (Handle<SpringBone> spring : {free, hinged, turned}) {
			spring->Simulate(1.0f / 60.0f);
		}
		for (u32 bone = 1; bone <= TAILS; ++bone) {
			hingedOff = Math::Max(hingedOff, std::abs(BoneWorld(hinged, bone).z));
			turnedOff = Math::Max(turnedOff, std::abs(BoneWorld(turned, bone).y));
			EXPECT_NEAR(Vec3::Distance(BoneWorld(hinged, bone), BoneWorld(hinged, bone - 1)), SEGMENT, 1e-3f) << bone;
		}
	}
	EXPECT_LT(hingedOff, 1e-4f);
	EXPECT_LT(turnedOff, 1e-4f);
	EXPECT_GT(BoneWorld(free, TAILS).z, 0.1f);
	EXPECT_LT(BoneWorld(hinged, TAILS).y, -0.4f);
	EXPECT_GT(BoneWorld(turned, TAILS).z, 0.4f);
}

TEST_F(SpringBoneFixture, HingeAxisCarriesChainAlongAxis) {
	SkeletonAsset* skeleton = MakeChain();
	Handle<SpringBone> free = MakeTail(skeleton, "free");
	Handle<SpringBone> hinged = MakeTail(skeleton, "hinged");
	hinged->SetHingeAxis(SpringBoneHingeAxis::Z);
	for (Handle<SpringBone> spring : {free, hinged}) {
		spring->SetStiffness(0.0f);
		spring->SetDamping(0.2f);
		spring->Simulate(0.0f);
		spring->GetOwner()->SetWorldPosition(Vec3(0.0f, 0.0f, 0.5f));
		Run(spring, 4.0f / 60.0f);
	}
	EXPECT_NEAR(BoneWorld(hinged, TAILS).z, 0.5f, 1e-4f);
	EXPECT_NEAR(BoneWorld(hinged, TAILS).x, SEGMENT * TAILS, 1e-4f);
	EXPECT_GT(std::abs(BoneWorld(free, TAILS).z - 0.5f), 0.05f);
}

#endif

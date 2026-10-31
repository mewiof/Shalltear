#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>
#include <vector>

#ifndef SHALLTEAR_SHARED
#include "AnimationPass.h"
#include "ClipSampler.h"
#include "PoseOps.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationEvent.h"
#include "Shalltear/Animation/Animator.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 50.0f;

	void RunUpdate(f32 delta) {
		FrameTime clockState;
		clockState.fixedDeltaTime = FIXED_STEP;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(delta);

		FrameStageArgs args;
		FrameStages::Run(FrameStage::Update, args);
	}

	std::vector<std::pair<f32, f32>> Runs(f32 from, f32 to, f32 length, AnimationLoopMode loop, bool* truncated = nullptr) {
		AnimationImpl::ClipSegment out[AnimationImpl::ANIMATION_MAX_CLIP_RUNS];
		const u32 count = AnimationImpl::SplitInterval(from, to, length, loop, out,
													   AnimationImpl::ANIMATION_MAX_CLIP_RUNS, truncated);
		std::vector<std::pair<f32, f32>> answer;
		for (u32 index = 0; index < count; ++index) {
			answer.emplace_back(out[index].begin, out[index].end);
		}
		return answer;
	}

	BoneTransform MakeBone(const Vec3& position, const Quat& rotation) {
		BoneTransform out;
		out.position = position;
		out.rotation = rotation;
		return out;
	}

} // anonymous namespace

TEST(ClipIntervalTest, ForwardFrameIsHalfOpenRun) {
	const auto runs = Runs(0.0f, 0.02f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(runs.size(), 1u);
	EXPECT_FLOAT_EQ(runs[0].first, 0.0f);
	EXPECT_FLOAT_EQ(runs[0].second, 0.02f);

	const auto next = Runs(0.02f, 0.04f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(next.size(), 1u);
	EXPECT_FLOAT_EQ(next[0].first, 0.02f);
	EXPECT_FLOAT_EQ(next[0].second, 0.04f);

	EXPECT_TRUE(Runs(0.4f, 0.4f, 1.0f, AnimationLoopMode::Loop).empty());
}

TEST(ClipIntervalTest, LoopSeamSplitsRuns) {
	const auto seam = Runs(0.98f, 1.02f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(seam.size(), 2u);
	EXPECT_NEAR(seam[0].first, 0.98f, 1e-5f);
	EXPECT_NEAR(seam[0].second, 1.0f, 1e-5f);
	EXPECT_NEAR(seam[1].first, 0.0f, 1e-5f);
	EXPECT_NEAR(seam[1].second, 0.02f, 1e-5f);

	const auto fast = Runs(0.5f, 3.5f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(fast.size(), 4u);
	EXPECT_NEAR(fast[0].first, 0.5f, 1e-5f);
	EXPECT_NEAR(fast[1].first, 0.0f, 1e-5f);
	EXPECT_NEAR(fast[1].second, 1.0f, 1e-5f);
	EXPECT_NEAR(fast[3].second, 0.5f, 1e-5f);
}

TEST(ClipIntervalTest, ReverseFrameTilesAcrossSeam) {
	const auto back = Runs(0.6f, 0.4f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(back.size(), 1u);
	EXPECT_GT(back[0].first, back[0].second);
	EXPECT_FLOAT_EQ(back[0].first, 0.6f);
	EXPECT_FLOAT_EQ(back[0].second, 0.4f);

	const auto under = Runs(0.02f, -0.02f, 1.0f, AnimationLoopMode::Loop);
	ASSERT_EQ(under.size(), 2u);
	EXPECT_NEAR(under[0].first, 0.02f, 1e-5f);
	EXPECT_NEAR(under[0].second, 0.0f, 1e-5f);
	EXPECT_NEAR(under[1].first, 1.0f, 1e-5f);
	EXPECT_NEAR(under[1].second, 0.98f, 1e-5f);
}

TEST(ClipIntervalTest, PingPongCrossesKeyTwice) {
	const auto period = Runs(0.0f, 2.0f, 1.0f, AnimationLoopMode::PingPong);
	ASSERT_EQ(period.size(), 2u);
	EXPECT_NEAR(period[0].first, 0.0f, 1e-5f);
	EXPECT_NEAR(period[0].second, 1.0f, 1e-5f);
	EXPECT_NEAR(period[1].first, 1.0f, 1e-5f);
	EXPECT_NEAR(period[1].second, 0.0f, 1e-5f);

	EXPECT_NEAR(AnimationImpl::WrapTime(1.2f, 1.0f, AnimationLoopMode::PingPong), 0.8f, 1e-5f);
	const auto inside = Runs(1.1f, 1.3f, 1.0f, AnimationLoopMode::PingPong);
	ASSERT_EQ(inside.size(), 1u);
	EXPECT_NEAR(inside[0].first, 0.9f, 1e-5f);
	EXPECT_NEAR(inside[0].second, 0.7f, 1e-5f);

	EXPECT_NEAR(AnimationImpl::WrapTime(-0.2f, 1.0f, AnimationLoopMode::PingPong), 0.2f, 1e-5f);
	const auto below = Runs(-0.1f, -0.3f, 1.0f, AnimationLoopMode::PingPong);
	ASSERT_EQ(below.size(), 1u);
	EXPECT_NEAR(below[0].first, 0.1f, 1e-5f);
	EXPECT_NEAR(below[0].second, 0.3f, 1e-5f);
}

TEST(ClipIntervalTest, NonLoopingClampedPastEndEmpty) {
	const auto over = Runs(0.9f, 1.4f, 1.0f, AnimationLoopMode::None);
	ASSERT_EQ(over.size(), 1u);
	EXPECT_FLOAT_EQ(over[0].first, 0.9f);
	EXPECT_FLOAT_EQ(over[0].second, 1.0f);

	EXPECT_TRUE(Runs(1.4f, 1.9f, 1.0f, AnimationLoopMode::None).empty());
}

TEST(ClipIntervalTest, AbsurdClockTruncatedAndReported) {
	bool truncated = false;
	const auto absurd = Runs(0.0f, 1.0e30f, 0.01f, AnimationLoopMode::Loop, &truncated);
	EXPECT_TRUE(truncated);
	EXPECT_LE(absurd.size(), static_cast<size_t>(AnimationImpl::ANIMATION_MAX_CLIP_RUNS));
	ASSERT_FALSE(absurd.empty());

	const auto big = Runs(0.0f, 100.0f, 0.01f, AnimationLoopMode::Loop, &truncated);
	EXPECT_TRUE(truncated);
	ASSERT_FALSE(big.empty());
	EXPECT_NEAR(big.back().second, AnimationImpl::WrapTime(100.0f, 0.01f, AnimationLoopMode::Loop), 1e-6f);

	EXPECT_TRUE(Runs(0.0f, 1.0f, 0.0f, AnimationLoopMode::Loop).empty());
	EXPECT_TRUE(Runs(0.0f, std::numeric_limits<f32>::quiet_NaN(), 1.0f, AnimationLoopMode::Loop).empty());
}

TEST(RootMotionOpsTest, DeltasInFrameCompose) {
	using namespace AnimationImpl;
	const BoneTransform start = MakeBone(Vec3(0.0f, 0.0f, 0.0f), Quat::Identity());
	const BoneTransform turned = MakeBone(Vec3(0.0f, 0.0f, 1.0f), Quat::AngleAxis(90.0f, Vec3::Up()));

	const BoneTransform first = DeltaBetween(start, turned);
	EXPECT_NEAR(first.position.z, 1.0f, 1e-5f);
	EXPECT_NEAR(Quat::Angle(first.rotation, Quat::AngleAxis(90.0f, Vec3::Up())), 0.0f, 1e-3f);

	BoneTransform second;
	second.position = Vec3(0.0f, 0.0f, 1.0f);
	const BoneTransform both = ComposeDelta(first, second);
	EXPECT_NEAR(both.position.x, 1.0f, 1e-5f);
	EXPECT_NEAR(both.position.z, 1.0f, 1e-5f);
	EXPECT_NEAR(Quat::Angle(both.rotation, Quat::AngleAxis(90.0f, Vec3::Up())), 0.0f, 1e-3f);

	const BoneTransform half = ScaleDelta(first, 0.5f);
	EXPECT_NEAR(half.position.z, 0.5f, 1e-5f);
	EXPECT_NEAR(Quat::Angle(half.rotation, Quat::AngleAxis(45.0f, Vec3::Up())), 0.0f, 1e-3f);
	EXPECT_NEAR(ScaleDelta(first, 0.0f).position.z, 0.0f, 1e-6f);
	EXPECT_NEAR(ScaleDelta(first, 1.0f).position.z, 1.0f, 1e-6f);
}

TEST(RootMotionOpsTest, AnchorConjugationMovesDeltaToObject) {
	using namespace AnimationImpl;
	const BoneTransform anchor = MakeBone(Vec3(2.0f, 0.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3::Up()));

	BoneTransform travel;
	travel.position = Vec3(0.0f, 0.0f, 1.0f);
	const BoneTransform object = ConjugateDelta(anchor, travel);

	EXPECT_NEAR(object.position.x, 1.0f, 1e-5f);
	EXPECT_NEAR(object.position.z, 0.0f, 1e-5f);

	BoneTransform turn;
	turn.position = Vec3(0.0f, 0.0f, 1.0f);
	turn.rotation = Quat::AngleAxis(30.0f, Vec3::Up());
	const BoneTransform delta = ConjugateDelta(anchor, turn);

	const Mat4 world = Mat4::Identity();
	const Mat4 reference = world * ComposeDelta(anchor, turn).ToMatrix();
	const Mat4 extracted = (world * delta.ToMatrix()) * anchor.ToMatrix();
	for (i32 row = 0; row < 4; ++row) {
		for (i32 column = 0; column < 4; ++column) {
			EXPECT_NEAR(extracted(row, column), reference(row, column), 1e-4f) << row << "][" << column << "]";
		}
	}
}

namespace {

	class EventProbeComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(EventProbeComponent, Component)
		SLT_REFLECT_METHOD(Ring)
		SLT_REFLECT_METHOD(RingWith)
		SLT_END_REFLECT()

		EventProbeComponent() = default;

		void Ring() { ++rings; }
		void RingWith(f32 volume) { loudness += volume; }

		i32 rings = 0;
		f32 loudness = 0.0f;
	};

	struct EventLog {
		std::vector<StringHash> names;
		std::vector<f32> times;
		std::vector<f32> payloads;
		std::vector<i32> layers;
		Animator* animator = nullptr;
		u32 tokenToDrop = 0;

		static void Record(const AnimationEvent& event, void* user) {
			EventLog& log = *static_cast<EventLog*>(user);
			log.names.push_back(event.name);
			log.times.push_back(event.time);
			log.payloads.push_back(event.value.x);
			log.layers.push_back(event.layer);
			if (log.tokenToDrop != 0 && log.animator) {
				log.animator->Unsubscribe(log.tokenToDrop);
				log.tokenToDrop = 0;
			}
		}

		static void Count(const AnimationEvent&, void* user) { ++*static_cast<i32*>(user); }
	};

	class AnimationEventFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_animation_event_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);
			EventProbeComponent::GetReflectType();

			_scene = SceneSystem::CreateScene("animation");
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

		AnimationClip* MakeClip(f32 length, AnimationLoopMode loop) {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://event.clip";
			clip->length = length;
			clip->loop = loop;
			_owned.push_back(clip);
			return clip;
		}

		static void AddEvent(AnimationClip& clip, const char* name, f32 at, f32 payload = 0.0f) {
			AnimationClip::Track track;
			track.kind = AnimationTrackKind::Event;
			track.interp = AnimationInterpolation::Step;
			track.name = str(name);
			track.times.push_back(at);
			track.values.push_back(Vec4(payload, 0.0f, 0.0f, 0.0f));
			clip.tracks.push_back(std::move(track));
		}

		static void AddMethod(AnimationClip& clip, const char* method, f32 at, StringHash component) {
			AnimationClip::Track track;
			track.kind = AnimationTrackKind::Method;
			track.interp = AnimationInterpolation::Step;
			track.name = str(method);
			track.componentType = component;
			track.times.push_back(at);
			track.values.push_back(Vec4{});
			clip.tracks.push_back(std::move(track));
		}

		Handle<Animator> MakeAnimator(AnimationClip* clip) {
			Handle<SceneObject> object = SceneSystem::CreateObject("actor", _scene);
			Handle<Animator> animator = object->AddComponent<Animator>();
			animator->SetClip(Handle<AnimationClip>(clip));
			return animator;
		}
	};

} // anonymous namespace

TEST_F(AnimationEventFixture, EventsAtEndsFireOnEndFrames) {
	AnimationClip* clip = MakeClip(0.5f, AnimationLoopMode::None);
	AddEvent(*clip, "Start", 0.0f);
	AddEvent(*clip, "Finish", 0.5f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	ASSERT_NE(animator->Subscribe(&EventLog::Record, &log), 0u);

	RunUpdate(0.02f);
	ASSERT_EQ(log.names.size(), 1u);
	EXPECT_EQ(log.names[0], StringHash("Start"));
	EXPECT_FLOAT_EQ(log.times[0], 0.0f);

	RunUpdate(0.02f);
	EXPECT_EQ(log.names.size(), 1u);

	for (i32 frame = 0; frame < 30; ++frame) {
		RunUpdate(0.02f);
	}
	ASSERT_EQ(log.names.size(), 2u);
	EXPECT_EQ(log.names[1], StringHash("Finish"));
	EXPECT_FALSE(animator->IsPlaying());

	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.02f);
	}
	EXPECT_EQ(log.names.size(), 2u);
}

TEST_F(AnimationEventFixture, SeamEventFiresOncePerLoop) {
	AnimationClip* clip = MakeClip(0.2f, AnimationLoopMode::Loop);
	AddEvent(*clip, "Seam", 0.19f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	for (i32 frame = 0; frame < 12; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_EQ(log.names.size(), 3u) << log.names.size();
	for (const StringHash& name : log.names) {
		EXPECT_EQ(name, StringHash("Seam"));
	}
}

TEST_F(AnimationEventFixture, EventsFireMoreAt10xSpeed) {
	AnimationClip* clip = MakeClip(0.1f, AnimationLoopMode::Loop);
	AddEvent(*clip, "Tick", 0.05f);

	Handle<Animator> slow = MakeAnimator(clip);
	i32 slowCount = 0;
	slow->Subscribe(&EventLog::Count, &slowCount);

	Handle<Animator> fast = MakeAnimator(clip);
	fast->SetSpeed(10.0f);
	i32 fastCount = 0;
	fast->Subscribe(&EventLog::Count, &fastCount);

	for (i32 frame = 0; frame < 20; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_EQ(slowCount, 10);
	EXPECT_EQ(fastCount, 100);
}

TEST_F(AnimationEventFixture, EventOncePerCrossingAtNegativeSpeed) {
	AnimationClip* clip = MakeClip(0.2f, AnimationLoopMode::Loop);
	AddEvent(*clip, "Back", 0.1f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	RunUpdate(0.02f);
	ASSERT_TRUE(log.names.empty());

	animator->SetSpeed(-1.0f);
	for (i32 frame = 0; frame < 20; ++frame) {
		RunUpdate(0.02f);
	}
	EXPECT_EQ(log.names.size(), 2u);
}

TEST_F(AnimationEventFixture, EventsInFrameFireInClockOrder) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AddEvent(*clip, "Second", 0.30f);
	AddEvent(*clip, "First", 0.10f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	RunUpdate(0.5f);
	ASSERT_EQ(log.names.size(), 2u);
	EXPECT_EQ(log.names[0], StringHash("First"));
	EXPECT_EQ(log.names[1], StringHash("Second"));

	log.names.clear();
	animator->SetSpeed(-1.0f);
	RunUpdate(0.5f);
	ASSERT_EQ(log.names.size(), 2u);
	EXPECT_EQ(log.names[0], StringHash("Second"));
	EXPECT_EQ(log.names[1], StringHash("First"));
}

TEST_F(AnimationEventFixture, SeekFiresNothingPlaybackDoes) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AddEvent(*clip, "Step", 0.1f);
	AddEvent(*clip, "Step", 0.5f);
	AddEvent(*clip, "Late", 0.9f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	animator->SetTime(0.8f);
	EXPECT_FLOAT_EQ(animator->GetTime(), 0.0f);

	RunUpdate(0.02f);
	ASSERT_TRUE(log.names.empty());

	animator->SetTime(0.8f);
	RunUpdate(0.02f);
	EXPECT_TRUE(log.names.empty());

	RunUpdate(0.2f);
	ASSERT_EQ(log.names.size(), 1u);
	EXPECT_EQ(log.names[0], StringHash("Late"));
}

TEST_F(AnimationEventFixture, TokenRemovesOneSubscriber) {
	AnimationClip* clip = MakeClip(0.1f, AnimationLoopMode::Loop);
	AddEvent(*clip, "Beat", 0.05f);

	Handle<Animator> animator = MakeAnimator(clip);
	i32 first = 0;
	i32 second = 0;
	const u32 firstToken = animator->Subscribe(&EventLog::Count, &first);
	const u32 secondToken = animator->Subscribe(&EventLog::Count, &second);
	ASSERT_NE(firstToken, 0u);
	ASSERT_NE(secondToken, 0u);
	EXPECT_NE(firstToken, secondToken);
	EXPECT_EQ(animator->GetSubscriberCount(), 2u);
	EXPECT_EQ(animator->Subscribe(nullptr, nullptr), 0u);

	RunUpdate(0.1f);
	EXPECT_EQ(first, 1);
	EXPECT_EQ(second, 1);

	EXPECT_TRUE(animator->Unsubscribe(firstToken));
	EXPECT_FALSE(animator->Unsubscribe(firstToken));
	EXPECT_FALSE(animator->Unsubscribe(0u));
	EXPECT_EQ(animator->GetSubscriberCount(), 1u);

	RunUpdate(0.1f);
	EXPECT_EQ(first, 1);
	EXPECT_EQ(second, 2);
}

TEST_F(AnimationEventFixture, SelfUnsubscribeKeepsDispatch) {
	AnimationClip* clip = MakeClip(0.1f, AnimationLoopMode::Loop);
	AddEvent(*clip, "Beat", 0.05f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	log.animator = animator.Get();
	const u32 token = animator->Subscribe(&EventLog::Record, &log);
	log.tokenToDrop = token;

	RunUpdate(0.1f);
	EXPECT_EQ(log.names.size(), 1u);
	EXPECT_EQ(animator->GetSubscriberCount(), 0u);

	RunUpdate(0.1f);
	EXPECT_EQ(log.names.size(), 1u);
}

TEST_F(AnimationEventFixture, PlayingLayersFireStoppedDont) {
	AnimationClip* base = MakeClip(0.1f, AnimationLoopMode::Loop);
	AddEvent(*base, "Base", 0.05f);
	AnimationClip* upper = MakeClip(0.1f, AnimationLoopMode::Loop);
	AddEvent(*upper, "Upper", 0.05f);

	Handle<Animator> animator = MakeAnimator(base);
	const i32 layer = animator->AddLayer();
	ASSERT_EQ(layer, 1);
	animator->Play(Handle<AnimationClip>(upper), 0.0f, layer);

	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	RunUpdate(0.1f);
	ASSERT_EQ(log.names.size(), 2u);
	EXPECT_EQ(log.layers[0], 0);
	EXPECT_EQ(log.layers[1], 1);

	animator->GetLayer(1)->Stop();
	log.names.clear();
	log.layers.clear();
	RunUpdate(0.1f);
	ASSERT_EQ(log.names.size(), 1u);
	EXPECT_EQ(log.layers[0], 0);
}

TEST_F(AnimationEventFixture, EventCarriesKeys4FloatsAndName) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AddEvent(*clip, "LeftFoot", 0.1f, 0.25f);
	AddEvent(*clip, "RightFoot", 0.2f, 0.75f);

	Handle<Animator> animator = MakeAnimator(clip);
	EventLog log;
	animator->Subscribe(&EventLog::Record, &log);

	RunUpdate(0.5f);
	ASSERT_EQ(log.names.size(), 2u);
	EXPECT_EQ(log.names[0], StringHash("LeftFoot"));
	EXPECT_FLOAT_EQ(log.payloads[0], 0.25f);
	EXPECT_EQ(log.names[1], StringHash("RightFoot"));
	EXPECT_FLOAT_EQ(log.payloads[1], 0.75f);
	EXPECT_FLOAT_EQ(log.times[1], 0.2f);
}

TEST_F(AnimationEventFixture, MethodTrackCallsArity0Only) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	const StringHash probeType = EventProbeComponent::GetReflectType()->name;
	AddMethod(*clip, "Ring", 0.1f, probeType);
	AddMethod(*clip, "RingWith", 0.2f, probeType);
	AddMethod(*clip, "NoSuchMethod", 0.3f, probeType);
	AddEvent(*clip, "Marker", 0.4f);

	Handle<SceneObject> object = SceneSystem::CreateObject("bell", _scene);
	Handle<EventProbeComponent> probe = object->AddComponent<EventProbeComponent>();
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.02f);

	EXPECT_EQ(animator->GetBoundTrackCount(), 2u);

	RunUpdate(0.5f);
	EXPECT_EQ(probe->rings, 1);
	EXPECT_FLOAT_EQ(probe->loudness, 0.0f);
}

TEST_F(AnimationEventFixture, MethodTrackSkipsDeadTarget) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::Loop);
	AddMethod(*clip, "Ring", 0.5f, EventProbeComponent::GetReflectType()->name);

	Handle<SceneObject> object = SceneSystem::CreateObject("bell", _scene);
	Handle<EventProbeComponent> probe = object->AddComponent<EventProbeComponent>();
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.6f);
	ASSERT_EQ(probe->rings, 1);

	SceneSystem::Destroy(Handle<Component>(probe.Get()));
	SceneSystem::EndFrame();
	RunUpdate(0.6f);
	EXPECT_FALSE(probe.IsValid());
	RunUpdate(0.6f);
	SUCCEED();
}

namespace {

	class RootMotionFixture : public AnimationEventFixture {
	protected:
		SkeletonAsset* MakeRig() {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			skeleton->boneNames.push_back("Root");
			skeleton->parents.push_back(-1);
			skeleton->restPose.push_back(BoneTransform{});
			skeleton->boneNames.push_back("Hips");
			skeleton->parents.push_back(0);
			BoneTransform hips;
			hips.position = Vec3(0.0f, 1.0f, 0.0f);
			skeleton->restPose.push_back(hips);
			_owned.push_back(skeleton);
			return skeleton;
		}

		AnimationClip* MakeWalk(f32 length, f32 distance, f32 degrees, bool bake, f32 lift = 0.0f,
								AnimationLoopMode loop = AnimationLoopMode::Loop) {
			AnimationClip* clip = MakeClip(length, loop);
			clip->bakeRootMotionIntoPose = bake;

			AnimationClip::Track position;
			position.kind = AnimationTrackKind::BonePosition;
			position.interp = AnimationInterpolation::Linear;
			position.name = "Root";
			position.times.push_back(0.0f);
			position.values.push_back(Vec4(0.0f, lift, 0.0f, 0.0f));
			position.times.push_back(length);
			position.values.push_back(Vec4(0.0f, lift, distance, 0.0f));
			clip->tracks.push_back(std::move(position));

			AnimationClip::Track rotation;
			rotation.kind = AnimationTrackKind::BoneRotation;
			rotation.interp = AnimationInterpolation::Linear;
			rotation.name = "Root";
			const Quat start = Quat::Identity();
			const Quat end = Quat::AngleAxis(degrees, Vec3::Up());
			rotation.times.push_back(0.0f);
			rotation.values.push_back(Vec4(start.x, start.y, start.z, start.w));
			rotation.times.push_back(length);
			rotation.values.push_back(Vec4(end.x, end.y, end.z, end.w));
			clip->tracks.push_back(std::move(rotation));
			return clip;
		}

		Handle<Animator> MakeCharacter(SkeletonAsset* skeleton, AnimationClip* clip, const char* name) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			Handle<Skeleton> rig = object->AddComponent<Skeleton>();
			rig->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			Handle<Animator> animator = object->AddComponent<Animator>();
			animator->SetClip(Handle<AnimationClip>(clip));
			return animator;
		}

		static Mat4 BoneWorld(Handle<Animator> animator, u32 bone) {
			SceneObject* object = animator->GetOwner().Get();
			const std::span<const Mat4> model = object->GetComponent<Skeleton>()->GetModelPose();
			return object->GetWorldMatrix() * model[bone];
		}
	};

} // anonymous namespace

TEST_F(RootMotionFixture, ExtractedMatchesBakedPose) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> baked = MakeCharacter(
		rig, MakeWalk(1.0f, 2.0f, 90.0f, true, 0.5f, AnimationLoopMode::None), "baked");
	Handle<Animator> extracted = MakeCharacter(
		rig, MakeWalk(1.0f, 2.0f, 90.0f, false, 0.5f, AnimationLoopMode::None), "extracted");
	extracted->SetApplyRootMotion(true);

	for (Handle<Animator> who : {baked, extracted}) {
		SceneObject* object = who->GetOwner().Get();
		object->SetLocalPosition(Vec3(3.0f, 0.0f, -1.0f));
		object->SetLocalRotation(Quat::AngleAxis(35.0f, Vec3::Up()));
	}

	for (i32 frame = 0; frame < 11; ++frame) {
		RunUpdate(0.04f);
	}
	ASSERT_TRUE(extracted->IsExtractingRootMotion());
	ASSERT_FALSE(baked->IsExtractingRootMotion());

	for (u32 bone = 0; bone < 2; ++bone) {
		const Mat4 reference = BoneWorld(baked, bone);
		const Mat4 moved = BoneWorld(extracted, bone);
		for (i32 row = 0; row < 4; ++row) {
			for (i32 column = 0; column < 4; ++column) {
				EXPECT_NEAR(moved(row, column), reference(row, column), 2e-3f) << "bone " << bone << ' ' << row << "][" << column << "]";
			}
		}
	}

	const Vec3 travelled = extracted->GetOwner()->GetLocalPosition() - baked->GetOwner()->GetLocalPosition();
	EXPECT_GT(travelled.Length(), 0.5f);
}

TEST_F(RootMotionFixture, ExtractedLoopKeepsTravelling) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> baked = MakeCharacter(rig, MakeWalk(0.2f, 1.0f, 0.0f, true), "baked");
	Handle<Animator> extracted = MakeCharacter(rig, MakeWalk(0.2f, 1.0f, 0.0f, false), "extracted");
	extracted->SetApplyRootMotion(true);

	for (i32 frame = 0; frame < 25; ++frame) {
		RunUpdate(0.04f);
	}

	const std::span<const BoneTransform> bakedPose =
		baked->GetOwner()->GetComponent<Skeleton>()->GetLocalPose();
	EXPECT_NEAR(bakedPose[0].position.z, 0.0f, 2e-2f);
	EXPECT_NEAR(baked->GetOwner()->GetLocalPosition().z, 0.0f, 1e-5f);

	EXPECT_NEAR(extracted->GetOwner()->GetLocalPosition().z, 5.0f, 5e-2f);
	const std::span<const BoneTransform> movedPose =
		extracted->GetOwner()->GetComponent<Skeleton>()->GetLocalPose();
	EXPECT_NEAR(movedPose[0].position.z, 0.0f, 1e-4f);
}

TEST_F(RootMotionFixture, DeltaPublishedWithApplyOff) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> animator = MakeCharacter(rig, MakeWalk(0.4f, 2.0f, 0.0f, false), "runner");
	animator->SetApplyRootMotion(false);

	SceneObject* object = animator->GetOwner().Get();
	const Vec3 start = object->GetLocalPosition();

	Vec3 controller{};
	f32 published = 0.0f;
	for (i32 frame = 0; frame < 10; ++frame) {
		RunUpdate(0.04f);
		const Vec3 delta = animator->GetRootMotionDelta();
		published += delta.Length();
		controller = controller + delta * 0.5f;
	}

	EXPECT_TRUE(animator->IsExtractingRootMotion());
	EXPECT_NEAR(object->GetLocalPosition().x, start.x, 1e-5f);
	EXPECT_NEAR(object->GetLocalPosition().z, start.z, 1e-5f);

	EXPECT_NEAR(published, 2.0f, 1e-2f);
	EXPECT_NEAR(controller.z, 1.0f, 1e-2f);

	const std::span<const BoneTransform> pose = object->GetComponent<Skeleton>()->GetLocalPose();
	ASSERT_GE(pose.size(), 1u);
	EXPECT_NEAR(pose[0].position.z, 0.0f, 1e-4f);
}

TEST_F(RootMotionFixture, BakedClipPosesRootAndPublishesNothing) {
	SkeletonAsset* rig = MakeRig();
	AnimationClip* clip = MakeWalk(0.4f, 2.0f, 0.0f, true);
	EXPECT_TRUE(AnimationClip{}.bakeRootMotionIntoPose);

	Handle<Animator> animator = MakeCharacter(rig, clip, "walker");
	animator->SetApplyRootMotion(true);
	SceneObject* object = animator->GetOwner().Get();
	const Vec3 start = object->GetLocalPosition();

	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.04f);
	}
	EXPECT_FALSE(animator->IsExtractingRootMotion());
	EXPECT_NEAR(animator->GetRootMotionDelta().Length(), 0.0f, 1e-6f);
	EXPECT_NEAR((object->GetLocalPosition() - start).Length(), 0.0f, 1e-6f);

	const std::span<const BoneTransform> pose = object->GetComponent<Skeleton>()->GetLocalPose();
	ASSERT_GE(pose.size(), 1u);
	EXPECT_GT(pose[0].position.z, 0.1f);
}

TEST_F(RootMotionFixture, LoopSeamAccumulatesTravel) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> animator = MakeCharacter(rig, MakeWalk(0.2f, 1.0f, 0.0f, false), "looper");
	animator->SetApplyRootMotion(false);

	f32 mostNegative = 0.0f;
	f32 total = 0.0f;
	for (i32 frame = 0; frame < 21; ++frame) {
		RunUpdate(0.03f);
		const f32 z = animator->GetRootMotionDelta().z;
		mostNegative = Math::Min(mostNegative, z);
		total += z;
	}
	EXPECT_GT(mostNegative, -1e-4f);
	EXPECT_NEAR(total, 3.15f, 2e-2f);
}

TEST_F(RootMotionFixture, LayersFoldRootDeltasByWeight) {
	SkeletonAsset* rig = MakeRig();
	AnimationClip* slow = MakeWalk(1.0f, 1.0f, 0.0f, false);
	AnimationClip* fast = MakeWalk(1.0f, 3.0f, 0.0f, false);

	Handle<Animator> animator = MakeCharacter(rig, slow, "blend");
	animator->SetApplyRootMotion(false);
	const i32 upper = animator->AddLayer();
	animator->Play(Handle<AnimationClip>(fast), 0.0f, upper);
	animator->GetLayer(upper)->weight = 0.5f;

	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().z, 0.2f, 5e-3f);

	ASSERT_TRUE(animator->GetLayer(upper)->SetMaskSubtree(*rig, "Hips", 1.0f));
	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().z, 0.1f, 5e-3f);
}

TEST_F(RootMotionFixture, ANegativeSpeedPublishesNegatedTravel) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> animator = MakeCharacter(rig, MakeWalk(1.0f, 2.0f, 0.0f, false), "reverse");
	animator->SetApplyRootMotion(false);

	RunUpdate(0.1f);
	const f32 forward = animator->GetRootMotionDelta().z;
	EXPECT_NEAR(forward, 0.2f, 5e-3f);

	animator->SetSpeed(-1.0f);
	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().z, -0.2f, 5e-3f);
}

TEST_F(RootMotionFixture, NoRootBonePublishesNothing) {
	SkeletonAsset* rig = MakeRig();
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::Loop);
	clip->bakeRootMotionIntoPose = false;

	AnimationClip::Track track;
	track.kind = AnimationTrackKind::BonePosition;
	track.interp = AnimationInterpolation::Linear;
	track.name = "Hips";
	track.times.push_back(0.0f);
	track.values.push_back(Vec4(0.0f, 1.0f, 0.0f, 0.0f));
	track.times.push_back(1.0f);
	track.values.push_back(Vec4(0.0f, 1.0f, 5.0f, 0.0f));
	clip->tracks.push_back(std::move(track));

	Handle<Animator> animator = MakeCharacter(rig, clip, "hipwalker");
	animator->SetApplyRootMotion(true);
	SceneObject* object = animator->GetOwner().Get();
	const Vec3 start = object->GetLocalPosition();

	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.04f);
	}
	EXPECT_NEAR(animator->GetRootMotionDelta().Length(), 0.0f, 1e-6f);
	EXPECT_NEAR((object->GetLocalPosition() - start).Length(), 0.0f, 1e-6f);

	const std::span<const BoneTransform> pose = object->GetComponent<Skeleton>()->GetLocalPose();
	ASSERT_GE(pose.size(), 2u);
	EXPECT_GT(pose[1].position.z, 0.5f);
}

TEST_F(RootMotionFixture, ScaledCharacterTravelsInUnits) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> baked = MakeCharacter(rig, MakeWalk(0.4f, 2.0f, 45.0f, true), "baked");
	Handle<Animator> extracted = MakeCharacter(rig, MakeWalk(0.4f, 2.0f, 45.0f, false), "extracted");
	extracted->SetApplyRootMotion(true);
	for (Handle<Animator> who : {baked, extracted}) {
		who->GetOwner()->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	}

	for (i32 frame = 0; frame < 7; ++frame) {
		RunUpdate(0.04f);
	}
	for (u32 bone = 0; bone < 2; ++bone) {
		const Mat4 reference = BoneWorld(baked, bone);
		const Mat4 moved = BoneWorld(extracted, bone);
		for (i32 row = 0; row < 4; ++row) {
			for (i32 column = 0; column < 4; ++column) {
				EXPECT_NEAR(moved(row, column), reference(row, column), 4e-3f) << bone << " drifted at [" << row << "][" << column << "]";
			}
		}
	}
}

TEST_F(RootMotionFixture, DisableZeroesDeltaEnableReAnchors) {
	SkeletonAsset* rig = MakeRig();
	Handle<Animator> animator = MakeCharacter(rig, MakeWalk(1.0f, 4.0f, 0.0f, false), "cycle");
	animator->SetApplyRootMotion(false);

	RunUpdate(0.1f);
	ASSERT_TRUE(animator->IsExtractingRootMotion());
	EXPECT_GT(animator->GetRootMotionDelta().z, 0.1f);

	animator->SetEnabled(false);
	EXPECT_FALSE(animator->IsExtractingRootMotion());
	EXPECT_NEAR(animator->GetRootMotionDelta().Length(), 0.0f, 1e-6f);

	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().Length(), 0.0f, 1e-6f);

	animator->SetEnabled(true);
	RunUpdate(0.1f);
	EXPECT_TRUE(animator->IsExtractingRootMotion());
	EXPECT_GT(animator->GetRootMotionDelta().z, 0.1f);
}

#endif

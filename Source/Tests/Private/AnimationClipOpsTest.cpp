#include "TestsPCH.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/Animation/AnimationClip.h"

#include "AnimationClipOps.h"
#include "ClipSampler.h"
#endif

#ifdef SHALLTEAR_SHARED
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/TextAsset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

namespace {

	AnimationClip::Track MakeCubicTrack(std::initializer_list<f32> values) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::Property;
		track.interp = AnimationInterpolation::CubicSpline;
		track.component = 0;
		track.name = "value";
		f32 time = 0.0f;
		for (f32 v : values) {
			track.times.push_back(time);
			track.values.push_back(Vec4{});
			track.values.push_back(Vec4{v, 0, 0, 0});
			track.values.push_back(Vec4{});
			time += 1.0f;
		}
		return track;
	}

	f32 SampleX(const AnimationClip::Track& track, f32 time) {
		return AnimationImpl::SampleTrack(track, time).x;
	}

} // anonymous namespace

TEST(AnimationClipOpsTest, SlicedCubicKeepsCurveThroughCut) {
	AnimationClip source;
	source.length = 2.0f;
	source.tracks.push_back(MakeCubicTrack({0.0f, 1.0f, 0.0f}));

	AnimationClip sliced;
	AnimationClipOps::SliceRange(source, 0.5f, 1.5f, sliced);
	ASSERT_EQ(sliced.tracks.size(), 1u);
	EXPECT_FLOAT_EQ(sliced.length, 1.0f);

	EXPECT_EQ(AnimationImpl::KeyCount(sliced.tracks[0]), 3u);

	f32 worst = 0.0f;
	f32 worstAt = 0.0f;
	for (i32 step = 0; step <= 40; ++step) {
		const f32 local = static_cast<f32>(step) / 40.0f;
		const f32 error = Math::Abs(SampleX(sliced.tracks[0], local) - SampleX(source.tracks[0], local + 0.5f));
		if (error > worst) {
			worst = error;
			worstAt = local;
		}
	}
	EXPECT_LT(worst, 1e-4f);
}

TEST(AnimationClipOpsTest, SlicedLinearExactArraysInStep) {
	AnimationClip source;
	AnimationClip::Track track;
	track.interp = AnimationInterpolation::Linear;
	track.kind = AnimationTrackKind::Property;
	track.name = "value";
	for (i32 k = 0; k < 3; ++k) {
		track.times.push_back(static_cast<f32>(k));
		track.values.push_back(Vec4{static_cast<f32>(k) * 2.0f, 0, 0, 0});
	}
	source.tracks.push_back(std::move(track));

	AnimationClip sliced;
	AnimationClipOps::SliceRange(source, 0.5f, 1.5f, sliced);
	ASSERT_EQ(sliced.tracks.size(), 1u);
	EXPECT_EQ(sliced.tracks[0].times.size(), sliced.tracks[0].values.size());
	for (i32 step = 0; step <= 10; ++step) {
		const f32 local = static_cast<f32>(step) / 10.0f;
		EXPECT_NEAR(SampleX(sliced.tracks[0], local), SampleX(source.tracks[0], local + 0.5f), 1e-5f);
	}
}

TEST(AnimationClipOpsTest, SlicedPropertyTrackKeepsBinding) {
	AnimationClip source;
	AnimationClip::Track track;
	track.kind = AnimationTrackKind::Property;
	track.interp = AnimationInterpolation::Linear;
	track.objectPath = "Panel/Icon";
	track.componentType = StringHash("RectTransform");
	track.name = "localPosition";
	track.component = 1;
	track.times.push_back(0.0f);
	track.values.push_back(Vec4{5.0f, 0, 0, 0});
	track.times.push_back(2.0f);
	track.values.push_back(Vec4{9.0f, 0, 0, 0});
	source.tracks.push_back(std::move(track));

	AnimationClip sliced;
	AnimationClipOps::SliceRange(source, 0.5f, 1.5f, sliced);
	ASSERT_EQ(sliced.tracks.size(), 1u);
	EXPECT_EQ(sliced.tracks[0].objectPath, "Panel/Icon");
	EXPECT_EQ(sliced.tracks[0].componentType, StringHash("RectTransform"));
	EXPECT_EQ(sliced.tracks[0].name, "localPosition");
	EXPECT_EQ(sliced.tracks[0].component, 1);
}

TEST(AnimationClipOpsTest, SlicedEmptyTrackStaysEmpty) {
	AnimationClip source;
	AnimationClip::Track track;
	track.kind = AnimationTrackKind::BoneRotation;
	track.interp = AnimationInterpolation::Linear;
	track.name = "Hips";
	source.tracks.push_back(std::move(track));

	AnimationClip sliced;
	AnimationClipOps::SliceRange(source, 0.5f, 1.5f, sliced);
	ASSERT_EQ(sliced.tracks.size(), 1u);
	EXPECT_TRUE(sliced.tracks[0].times.empty());
	EXPECT_TRUE(sliced.tracks[0].values.empty());
}

namespace {

	i32 SweepWorstAt(const AnimationClip::Track& before, const AnimationClip::Track& after, f32 from, f32 to,
					 f32& worst) {
		worst = 0.0f;
		i32 worstStep = -1;
		for (i32 step = 0; step <= 40; ++step) {
			const f32 t = from + (to - from) * static_cast<f32>(step) / 40.0f;
			const f32 error = Math::Abs(SampleX(after, t) - SampleX(before, t));
			if (error > worst) {
				worst = error;
				worstStep = step;
			}
		}
		return worstStep;
	}

} // anonymous namespace

TEST(AnimationClipOpsTest, KeyInsideCubicKeepsCurve) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({0.0f, 1.0f, 0.0f}));
	const AnimationClip::Track before = clip.tracks[0];

	const i32 key = AnimationClipOps::AddKey(clip, 0, 0.35f);
	ASSERT_EQ(key, 1);
	EXPECT_EQ(AnimationImpl::KeyCount(clip.tracks[0]), 4u);
	EXPECT_FLOAT_EQ(clip.tracks[0].times[1], 0.35f);

	f32 worst = 0.0f;
	const i32 at = SweepWorstAt(before, clip.tracks[0], 0.0f, 2.0f, worst);
	EXPECT_LT(worst, 1e-4f);
}

TEST(AnimationClipOpsTest, KeyOnExistingKeyReturnsIt) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({0.0f, 1.0f, 0.0f}));
	EXPECT_EQ(AnimationClipOps::AddKey(clip, 0, 1.0f), 1);
	EXPECT_EQ(AnimationImpl::KeyCount(clip.tracks[0]), 3u);
	EXPECT_EQ(AnimationClipOps::AddKey(clip, 0, 1.0002f), 1);
	EXPECT_EQ(AnimationImpl::KeyCount(clip.tracks[0]), 3u);
}

TEST(AnimationClipOpsTest, KeyMovedPastNeighbourRenumbers) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({10.0f, 20.0f, 30.0f}));

	const i32 moved = AnimationClipOps::MoveKey(clip, 0, 0, 2.5f);
	ASSERT_EQ(moved, 2);

	const AnimationClip::Track& track = clip.tracks[0];
	ASSERT_EQ(AnimationImpl::KeyCount(track), 3u);
	EXPECT_FLOAT_EQ(track.times[0], 1.0f);
	EXPECT_FLOAT_EQ(track.times[1], 2.0f);
	EXPECT_FLOAT_EQ(track.times[2], 2.5f);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(track, 0).x, 20.0f);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(track, 1).x, 30.0f);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(track, 2).x, 10.0f);
}

TEST(AnimationClipOpsTest, RemovingCubicKeyTakes3Entries) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({10.0f, 20.0f, 30.0f}));
	ASSERT_TRUE(AnimationClipOps::RemoveKey(clip, 0, 1));

	const AnimationClip::Track& track = clip.tracks[0];
	EXPECT_EQ(track.times.size(), 2u);
	EXPECT_EQ(track.values.size(), 6u);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(track, 0).x, 10.0f);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(track, 1).x, 30.0f);
}

TEST(AnimationClipOpsTest, TrackAddedTwiceReturnsSame) {
	AnimationClip clip;
	AnimationClipOps::TrackAddress address;
	address.kind = AnimationTrackKind::Property;
	address.objectPath = "";
	address.componentType = StringHash("Transform");
	address.name = "_localPosition";
	address.component = 0;

	const i32 first = AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Linear);
	EXPECT_EQ(AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Linear), first);
	EXPECT_EQ(clip.tracks.size(), 1u);

	address.component = 1;
	const i32 second = AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Linear);
	EXPECT_NE(second, first);
	EXPECT_EQ(clip.tracks.size(), 2u);
}

TEST(AnimationClipOpsTest, OnlyTrackEditBumpsGeneration) {
	AnimationClip clip;
	AnimationClipOps::TrackAddress address;
	address.name = "value";
	address.component = 0;

	const u32 atStart = clip.generation;
	const i32 track = AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Linear);
	ASSERT_GE(track, 0);
	EXPECT_GT(clip.generation, atStart);

	const u32 afterAdd = clip.generation;
	AnimationClipOps::AddKey(clip, track, 0.0f);
	AnimationClipOps::AddKey(clip, track, 1.0f);
	AnimationClipOps::MoveKey(clip, track, 0, 0.5f);
	AnimationClipOps::SetKeyValue(clip, track, 0, Vec4{7.0f, 0, 0, 0});
	AnimationClipOps::RemoveKey(clip, track, 0);
	EXPECT_EQ(clip.generation, afterAdd);

	AnimationClipOps::RemoveTrack(clip, track);
	EXPECT_GT(clip.generation, afterAdd);
}

TEST(AnimationClipOpsTest, EventTrackRenameBumpsAndRefusesCollision) {
	AnimationClip clip;
	AnimationClipOps::TrackAddress address;
	address.kind = AnimationTrackKind::Event;
	address.name = "Footstep";
	ASSERT_GE(AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Step), 0);
	address.name = "Land";
	ASSERT_GE(AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Step), 0);

	const u32 before = clip.generation;
	ASSERT_TRUE(AnimationClipOps::SetTrackName(clip, 0, "Footfall"));
	EXPECT_EQ(clip.tracks[0].name, "Footfall");
	EXPECT_GT(clip.generation, before);

	const u32 after = clip.generation;
	EXPECT_FALSE(AnimationClipOps::SetTrackName(clip, 1, "Footfall"));
	EXPECT_EQ(clip.tracks[1].name, "Land");
	EXPECT_EQ(clip.generation, after);
	EXPECT_FALSE(AnimationClipOps::SetTrackName(clip, 1, ""));
}

TEST(AnimationClipOpsTest, EventKeyStartsWithZeroPayload) {
	AnimationClip clip;
	AnimationClipOps::TrackAddress address;
	address.kind = AnimationTrackKind::Event;
	address.name = "Footstep";
	const i32 track = AnimationClipOps::AddTrack(clip, address, AnimationInterpolation::Step);
	ASSERT_GE(track, 0);

	ASSERT_GE(AnimationClipOps::AddKey(clip, track, 0.0f), 0);
	ASSERT_TRUE(AnimationClipOps::SetKeyValue(clip, track, 0, Vec4{7.0f, 8.0f, 9.0f, 10.0f}));
	ASSERT_EQ(AnimationClipOps::AddKey(clip, track, 1.0f), 1);

	const Vec4 second = AnimationImpl::KeyValue(clip.tracks[static_cast<size_t>(track)], 1);
	EXPECT_FLOAT_EQ(second.x, 0.0f);
	EXPECT_FLOAT_EQ(second.y, 0.0f);
	EXPECT_FLOAT_EQ(second.w, 0.0f);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(clip.tracks[static_cast<size_t>(track)], 0).x, 7.0f);
}

TEST(AnimationClipOpsTest, AutoTangentFlatAtTurningPoint) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({1.0f, 0.0f, 3.0f}));
	ASSERT_TRUE(AnimationClipOps::SetInterpolation(clip, 0, AnimationInterpolation::Linear));
	ASSERT_TRUE(AnimationClipOps::SetInterpolation(clip, 0, AnimationInterpolation::CubicSpline));

	EXPECT_FLOAT_EQ(AnimationClipOps::AutoTangent(clip.tracks[0], 1).x, 0.0f);

	f32 lowest = 1.0f;
	f32 lowestAt = 0.0f;
	for (i32 step = 0; step <= 80; ++step) {
		const f32 t = static_cast<f32>(step) / 40.0f;
		const f32 v = SampleX(clip.tracks[0], t);
		if (v < lowest) {
			lowest = v;
			lowestAt = t;
		}
	}
	EXPECT_GE(lowest, -1e-4f);
}

TEST(AnimationClipOpsTest, AutoTangentMonotoneIsCentredSecant) {
	AnimationClip clip;
	clip.tracks.push_back(MakeCubicTrack({0.0f, 1.0f, 2.0f}));
	EXPECT_FLOAT_EQ(AnimationClipOps::AutoTangent(clip.tracks[0], 1).x, 1.0f);
	EXPECT_FLOAT_EQ(AnimationClipOps::AutoTangent(clip.tracks[0], 0).x, 1.0f);
	EXPECT_FLOAT_EQ(AnimationClipOps::AutoTangent(clip.tracks[0], 2).x, 1.0f);
}

TEST(AnimationClipOpsTest, InterpolationChangeKeepsKeys) {
	AnimationClip clip;
	AnimationClip::Track track;
	track.interp = AnimationInterpolation::Linear;
	track.kind = AnimationTrackKind::Property;
	track.name = "value";
	for (i32 k = 0; k < 3; ++k) {
		track.times.push_back(static_cast<f32>(k));
		track.values.push_back(Vec4{static_cast<f32>(k) * 4.0f, 0, 0, 0});
	}
	clip.tracks.push_back(std::move(track));

	ASSERT_TRUE(AnimationClipOps::SetInterpolation(clip, 0, AnimationInterpolation::CubicSpline));
	EXPECT_EQ(clip.tracks[0].values.size(), 9u);
	EXPECT_EQ(clip.tracks[0].times.size(), 3u);
	for (size_t k = 0; k < 3; ++k) {
		EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(clip.tracks[0], k).x, static_cast<f32>(k) * 4.0f) << "key " << k;
	}
	EXPECT_FLOAT_EQ(AnimationImpl::KeyOutTangent(clip.tracks[0], 1).x, 4.0f);

	ASSERT_TRUE(AnimationClipOps::SetInterpolation(clip, 0, AnimationInterpolation::Step));
	EXPECT_EQ(clip.tracks[0].values.size(), 3u);
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(clip.tracks[0], 2).x, 8.0f);
	EXPECT_FALSE(AnimationClipOps::SetInterpolation(clip, 0, AnimationInterpolation::Step));
}

TEST(AnimationClipOpsTest, TangentOnStride1TrackRefused) {
	AnimationClip clip;
	AnimationClip::Track track;
	track.interp = AnimationInterpolation::Linear;
	track.name = "value";
	track.times.push_back(0.0f);
	track.values.push_back(Vec4{7.0f, 0, 0, 0});
	clip.tracks.push_back(std::move(track));

	EXPECT_FALSE(AnimationClipOps::SetKeyTangents(clip, 0, 0, Vec4{9, 0, 0, 0}, Vec4{9, 0, 0, 0}));
	EXPECT_FLOAT_EQ(AnimationImpl::KeyValue(clip.tracks[0], 0).x, 7.0f);
}

TEST(AnimationClipOpsTest, DerivedLengthIsLastKey) {
	AnimationClip clip;
	clip.length = 99.0f;
	clip.tracks.push_back(MakeCubicTrack({0.0f, 1.0f}));
	AnimationClip::Track longer;
	longer.interp = AnimationInterpolation::Linear;
	longer.name = "other";
	longer.times.push_back(0.0f);
	longer.values.push_back(Vec4{});
	longer.times.push_back(3.5f);
	longer.values.push_back(Vec4{});
	clip.tracks.push_back(std::move(longer));

	EXPECT_FLOAT_EQ(AnimationClipOps::DerivedLength(clip), 3.5f);
	EXPECT_TRUE(AnimationClipOps::RecomputeLength(clip));
	EXPECT_FLOAT_EQ(clip.length, 3.5f);
	EXPECT_FALSE(AnimationClipOps::RecomputeLength(clip));
}

TEST(AnimationClipOpsTest, TimeSnapsToFrameWhenRateKnown) {
	AnimationClip clip;
	clip.frameRate = 60.0f;
	EXPECT_NEAR(AnimationClipOps::SnapTime(clip, 0.37f), 22.0f / 60.0f, 1e-6f);
	EXPECT_NEAR(AnimationClipOps::SnapTime(clip, 0.0f), 0.0f, 1e-6f);

	clip.frameRate = 0.0f;
	EXPECT_FLOAT_EQ(AnimationClipOps::SnapTime(clip, 0.37f), 0.37f);
}

#endif

#ifdef SHALLTEAR_SHARED

using namespace UITest;

namespace {

	constexpr const char* PACKAGE_TARGET = "Animation";

	class MethodPickProbeComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(MethodPickProbeComponent, Component)
		SLT_REFLECT_METHOD(Ring)
		SLT_REFLECT_METHOD(RingWith)
		SLT_END_REFLECT()

		MethodPickProbeComponent() = default;

		void Ring() { ++rings; }
		void RingWith(f32 volume) { loudness += volume; }

		i32 rings = 0;
		f32 loudness = 0.0f;
	};

	class AnimationCurvePanelFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_curve_panel_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			ASSERT_NE(_moduleBase, nullptr);
			AssetDatabase::Initialize();
			MethodPickProbeComponent::GetReflectType();

			_scene = SceneSystem::CreateScene("curve-panel");
		}

		void TearDown() override {
			EditorExtensions::Unload(_moduleBase);
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorAssetOps::Terminate();
			EditorSceneOps::Terminate();
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid StageClip(const char* name, std::string_view json) {
			std::filesystem::create_directories(_root / "Assets");
			const std::filesystem::path file = _root / "Assets" / name;
			std::ofstream out(file, std::ios::binary);
			out.write(json.data(), static_cast<std::streamsize>(json.size()));
			out.close();
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path(str("project://Assets/") + name));
		}

		str ReadClip(const char* name) const {
			std::ifstream in(_root / "Assets" / name, std::ios::binary);
			return str((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		}
	};

	constexpr std::string_view EmptyClipJSON = R"({
"length": 0, "frameRate": 60, "loop": "None", "additiveReferenceTime": 0,
"bakeRootMotionIntoPose": true, "tracks": []
})";

	constexpr std::string_view NoRateClipJSON = R"({
"length": 2, "frameRate": 0, "loop": "None", "additiveReferenceTime": 0,
"bakeRootMotionIntoPose": true,
"tracks": [{"kind": "Property", "interp": "Linear", "component": 0, "objectPath": "",
"componentType": "", "name": "_localPosition",
"times": [0, 2], "values": [[0,0,0,0],[5,0,0,0]]}]
})";

	struct PanelHarness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit PanelHarness(const UI::ContextDesc& desc) : context(desc, &fonts) {}

		bool Open(const TypeInfo* type) {
			panel = PanelManager::OpenPanel(type);
			if (!panel.IsValid()) {
				return false;
			}
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel->BuildContent(context, root);
			context.Update();
			panel->OnTick(0.016f);
			context.Update();
			panel->OnTick(0.016f);
			context.Update();
			return true;
		}

		void Settle() {
			panel->OnTick(0.016f);
			context.Update();
		}
	};

	void ClickAt(UI::Context& context, i32 x, i32 y) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		context.InjectInput(move);
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Left;
		down.mousePosPx = {x, y};
		context.InjectInput(down);
		InputEvent up{};
		up.type = InputEventType::MouseUp;
		up.button = MouseButton::Left;
		up.mousePosPx = {x, y};
		context.InjectInput(up);
		context.Update();
	}

	void DoubleClickAt(UI::Context& context, i32 x, i32 y) {
		ClickAt(context, x, y);
		ClickAt(context, x, y);
	}

	void DragAt(UI::Context& context, i32 x, i32 y, i32 dx, i32 dy) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		context.InjectInput(move);
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Left;
		down.mousePosPx = {x, y};
		context.InjectInput(down);
		context.Update();
		for (i32 step = 1; step <= 2; ++step) {
			InputEvent travel{};
			travel.type = InputEventType::MouseMove;
			travel.mousePosPx = {x + dx * step / 2, y + dy * step / 2};
			context.InjectInput(travel);
			context.Update();
		}
		InputEvent up{};
		up.type = InputEventType::MouseUp;
		up.button = MouseButton::Left;
		up.mousePosPx = {x + dx, y + dy};
		context.InjectInput(up);
		context.Update();
	}

	void TypeIntoField(UI::Context& context, UI::ElementHandle field, std::string_view text) {
		ClickElement(context, field);
		InputEvent selectAll{};
		selectAll.type = InputEventType::KeyDown;
		selectAll.key = Key::A;
		selectAll.mods = KeyMod::Ctrl;
		context.InjectInput(selectAll);
		for (char c : text) {
			InputEvent typed{};
			typed.type = InputEventType::Text;
			typed.codepoint = static_cast<char32_t>(c);
			context.InjectInput(typed);
		}
		InputEvent commit{};
		commit.type = InputEventType::KeyDown;
		commit.key = Key::Enter;
		context.InjectInput(commit);
		context.Update();
	}

	u32 CountOccurrences(std::string_view text, std::string_view needle) {
		u32 count = 0;
		for (size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) {
			++count;
		}
		return count;
	}

	constexpr f64 NO_NUMBER = -99999.0;

	f64 FirstNumberIn(std::string_view text, std::string_view key) {
		const str needle = str("\"") + str(key) + "\": [";
		size_t at = text.find(needle);
		while (at != std::string_view::npos) {
			size_t cursor = at + needle.size();
			while (cursor < text.size() &&
				   (text[cursor] == ' ' || text[cursor] == '\n' || text[cursor] == '\t' || text[cursor] == '\r')) {
				++cursor;
			}
			if (cursor < text.size() && text[cursor] != ']') {
				size_t end = cursor;
				while (end < text.size() && (text[end] == '-' || text[end] == '.' || text[end] == 'e' ||
											 (text[end] >= '0' && text[end] <= '9'))) {
					++end;
				}
				if (end > cursor) {
					return std::atof(str(text.substr(cursor, end - cursor)).c_str());
				}
			}
			at = text.find(needle, at + 1);
		}
		return NO_NUMBER;
	}

	f64 LastNumberIn(std::string_view text, std::string_view key) {
		const str needle = str("\"") + str(key) + "\": [";
		size_t at = text.find(needle);
		while (at != std::string_view::npos) {
			const size_t open = at + needle.size();
			const size_t close = text.find(']', open);
			if (close != std::string_view::npos && text.substr(open, close - open).find_first_of("0123456789") !=
													   std::string_view::npos) {
				std::string_view body = text.substr(open, close - open);
				const size_t lastComma = body.find_last_of(',');
				if (lastComma != std::string_view::npos) {
					body = body.substr(lastComma + 1);
				}
				const size_t first = body.find_first_of("-0123456789");
				if (first != std::string_view::npos) {
					return std::atof(str(body.substr(first)).c_str());
				}
			}
			at = text.find(needle, at + 1);
		}
		return NO_NUMBER;
	}

} // anonymous namespace

TEST_F(AnimationCurvePanelFixture, PackageContributesAnimationPanelTypeThatOpens) {
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());
	EXPECT_EQ(panel->GetTitleBase(), "Animation");
}

TEST_F(AnimationCurvePanelFixture, ToolbarAddsPropertyKeysSaved) {
	Handle<SceneObject> panelObject = SceneSystem::CreateObject("Panel", _scene);
	ASSERT_TRUE(panelObject.IsValid());

	const Guid guid = StageClip("Slide.anim", EmptyClipJSON);
	ASSERT_TRUE(guid.IsValid());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	EditorSelection::Select(Handle<Object>(panelObject.Get()));
	harness.Settle();
	EditorSelection::SelectAsset(guid);
	harness.Settle();

	ASSERT_TRUE(harness.context.IsAlive(FindByTextContaining(harness.context, harness.root, "Slide.anim")));
	ASSERT_TRUE(harness.context.IsAlive(FindByTextContaining(harness.context, harness.root, "target: Panel")));

	UI::ElementHandle addButton = FindByText(harness.context, harness.root, "Add Property");
	ASSERT_TRUE(harness.context.IsAlive(addButton));
	ClickElement(harness.context, addButton);

	UI::ElementHandle transform = FindByText(harness.context, harness.context.GetRoot(), "Transform");
	ASSERT_TRUE(harness.context.IsAlive(transform));
	ClickElement(harness.context, transform);

	UI::ElementHandle field = FindByText(harness.context, harness.context.GetRoot(), "localPosition");
	ASSERT_TRUE(harness.context.IsAlive(field));
	ClickElement(harness.context, field);
	harness.Settle();

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "localPosition.x")));
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "localPosition.z")));

	UI::ElementHandle sheet = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(sheet));
	const UI::Rect area = harness.context.GetLayout(sheet).content;
	const f32 scale = harness.context.GetContentScale();
	const auto rowCentreY = [&](i32 row) {
		return static_cast<i32>(area.y + (22.0f + 18.0f * static_cast<f32>(row) + 9.0f) * scale);
	};

	DoubleClickAt(harness.context, static_cast<i32>(area.x + 220.0f), rowCentreY(0));
	harness.Settle();
	DoubleClickAt(harness.context, static_cast<i32>(area.x + 40.0f), rowCentreY(2));
	harness.Settle();

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);

	const str written = ReadClip("Slide.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_EQ(CountOccurrences(written, "\"_localPosition\""), 3u);
	EXPECT_EQ(CountOccurrences(written, "\"times\": []"), 1u);
	EXPECT_GT(LastNumberIn(written, "times"), 0.0);
	EXPECT_EQ(written.find("\"length\": 0,"), str::npos);
}

TEST_F(AnimationCurvePanelFixture, KeyDragWritesTimeToAsset) {
	const Guid guid = StageClip("Drag.anim", NoRateClipJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "localPosition.x")));

	UI::ElementHandle sheet = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(sheet));
	const UI::Rect area = harness.context.GetLayout(sheet).content;
	const f32 scale = harness.context.GetContentScale();
	const i32 rowY = static_cast<i32>(area.y + (22.0f + 9.0f) * scale);

	const i32 firstKeyX = static_cast<i32>(area.x + area.w * 0.05f);
	DragAt(harness.context, firstKeyX, rowY, static_cast<i32>(area.w * 0.2f), 0);
	harness.Settle();

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);

	const str written = ReadClip("Drag.anim");
	ASSERT_FALSE(written.empty());
	const f64 firstKey = FirstNumberIn(written, "times");
	ASSERT_NE(firstKey, NO_NUMBER);
	EXPECT_GT(firstKey, 0.0);
	EXPECT_LT(firstKey, 2.0);
}

TEST_F(AnimationCurvePanelFixture, RulerCountsFramesOrSeconds) {
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);

	{
		const Guid guid = StageClip("Rated.anim", R"({"length": 2, "frameRate": 60, "tracks": []})");
		ASSERT_TRUE(guid.IsValid());
		EditorSelection::SelectAsset(guid);
		UI::ContextDesc desc;
		desc.sizePx = {1200, 700};
		PanelHarness harness(desc);
		ASSERT_TRUE(harness.Open(type));
		harness.Settle();
		EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "120")));
		EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "1.5")));
	}

	{
		const Guid guid = StageClip("Unrated.anim", R"({"length": 2, "frameRate": 0, "tracks": []})");
		ASSERT_TRUE(guid.IsValid());
		EditorSelection::SelectAsset(guid);
		UI::ContextDesc desc;
		desc.sizePx = {1200, 700};
		PanelHarness harness(desc);
		ASSERT_TRUE(harness.Open(type));
		harness.Settle();
		EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "120")));
		EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "1.5")));
	}
}

TEST_F(AnimationCurvePanelFixture, TangentHandleShapesKeyFileKeepsIt) {
	const Guid guid = StageClip("Shape.anim", R"({"length": 2, "frameRate": 0,
"tracks": [{"kind": "Property", "interp": "CubicSpline", "component": 0, "objectPath": "",
"componentType": "", "name": "_localPosition",
"times": [0, 1, 2],
"values": [[0,0,0,0],[0,0,0,0],[0,0,0,0], [0,0,0,0],[4,0,0,0],[0,0,0,0], [0,0,0,0],[0,0,0,0],[0,0,0,0]]}]})");
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	UI::ElementHandle row = FindByText(harness.context, harness.root, "localPosition.x");
	ASSERT_TRUE(harness.context.IsAlive(row));
	ClickElement(harness.context, row);
	harness.Settle();

	UI::ElementHandle curves = FindByText(harness.context, harness.root, "Curves");
	ASSERT_TRUE(harness.context.IsAlive(curves));
	ClickElement(harness.context, curves);
	harness.Settle();

	UI::ElementHandle sheet = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(sheet));
	const UI::Rect area = harness.context.GetLayout(sheet).content;
	const f32 scale = harness.context.GetContentScale();
	const i32 keyX = static_cast<i32>(area.x + area.w * 0.05f + area.w * 0.9f * 0.5f);
	const f32 usable = area.h - 22.0f * scale;
	const i32 keyY = static_cast<i32>(area.y + 22.0f * scale + usable * (15.0f / 130.0f));

	ClickAt(harness.context, keyX, keyY);
	harness.Settle();
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Tangent")));

	const i32 handleX = keyX + static_cast<i32>(44.0f * scale);
	DragAt(harness.context, handleX, keyY, 0, -static_cast<i32>(usable * 0.25f));
	harness.Settle();

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);

	const str written = ReadClip("Shape.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_EQ(CountOccurrences(written, "\"interp\": \"CubicSpline\""), 1u);
	EXPECT_EQ(CountOccurrences(written, "\t\t\t\t\t0,\n\t\t\t\t\t0,\n\t\t\t\t\t0,\n\t\t\t\t\t0\n"), 6u);
}

TEST_F(AnimationCurvePanelFixture, InterpolationColumnWritesLoadableFile) {
	const Guid guid = StageClip("Interp.anim", R"({"length": 2, "frameRate": 0,
"tracks": [{"kind": "Property", "interp": "Linear", "component": 0, "objectPath": "",
"componentType": "", "name": "_localPosition",
"times": [0, 1, 2], "values": [[0,0,0,0],[4,0,0,0],[0,0,0,0]]}]})");
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "localPosition.x"));
	harness.Settle();

	UI::ElementHandle dropdown = FindByTextContaining(harness.context, harness.root, "Linear");
	ASSERT_TRUE(harness.context.IsAlive(dropdown));
	ClickElement(harness.context, dropdown);
	UI::ElementHandle cubic = FindByTextContaining(harness.context, harness.context.GetRoot(), "Cubic");
	ASSERT_TRUE(harness.context.IsAlive(cubic));
	ClickElement(harness.context, cubic);
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadClip("Interp.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"interp\": \"CubicSpline\""), str::npos);
	EXPECT_EQ(CountOccurrences(written, "\t\t\t\t["), 9u);
}

TEST_F(AnimationCurvePanelFixture, ToolbarEventTrackReachesFile) {
	Handle<SceneObject> panelObject = SceneSystem::CreateObject("Panel", _scene);
	ASSERT_TRUE(panelObject.IsValid());
	const Guid guid = StageClip("Signals.anim", EmptyClipJSON);
	ASSERT_TRUE(guid.IsValid());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	EditorSelection::Select(Handle<Object>(panelObject.Get()));
	harness.Settle();
	EditorSelection::SelectAsset(guid);
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Add Property"));
	UI::ElementHandle eventRow = FindByText(harness.context, harness.context.GetRoot(), "Event");
	ASSERT_TRUE(harness.context.IsAlive(eventRow));
	ClickElement(harness.context, eventRow);
	harness.Settle();
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "New Event")));

	UI::ElementHandle sheet = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(sheet));
	const UI::Rect area = harness.context.GetLayout(sheet).content;
	const f32 scale = harness.context.GetContentScale();
	DoubleClickAt(harness.context, static_cast<i32>(area.x + 200.0f),
				  static_cast<i32>(area.y + (22.0f + 9.0f) * scale));
	harness.Settle();

	UI::ElementHandle nameField;
	for (u32 skip = 0; skip < 8; ++skip) {
		UI::ElementHandle candidate = FindByClass(harness.context, harness.root, StringHash("slt-textfield"), skip);
		if (!harness.context.IsAlive(candidate)) {
			break;
		}
		if (harness.context.GetTextString(candidate) == "New Event") {
			nameField = candidate;
			break;
		}
	}
	ASSERT_TRUE(harness.context.IsAlive(nameField));
	TypeIntoField(harness.context, nameField, "Footstep");
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadClip("Signals.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"kind\": \"Event\""), str::npos);
	EXPECT_NE(written.find("\"name\": \"Footstep\""), str::npos);
	EXPECT_GT(FirstNumberIn(written, "times"), 0.0);
}

namespace {
	bool AcceptCue(const AssetCue&) { return true; }
} // namespace

TEST_F(AnimationCurvePanelFixture, AudioKeyTakesDroppedSound) {
	char owner = 0;
	ASSERT_TRUE(AssetCues::Register(TextAsset::GetReflectType(), &AcceptCue, &owner));
	Handle<SceneObject> panelObject = SceneSystem::CreateObject("Walker", _scene);
	ASSERT_TRUE(panelObject.IsValid());
	std::filesystem::create_directories(_root / "Assets");
	{
		std::ofstream out(_root / "Assets" / "Step.txt", std::ios::binary);
		out << "step";
	}
	const Guid guid = StageClip("Walk.anim", EmptyClipJSON);
	ASSERT_TRUE(guid.IsValid());
	const Guid step = AssetDatabase::LookupGuid(Path("project://Assets/Step.txt"));
	ASSERT_TRUE(step.IsValid());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	EditorSelection::Select(Handle<Object>(panelObject.Get()));
	harness.Settle();
	EditorSelection::SelectAsset(guid);
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Add Property"));
	UI::ElementHandle audioRow = FindByText(harness.context, harness.context.GetRoot(), "Audio");
	ASSERT_TRUE(harness.context.IsAlive(audioRow));
	ClickElement(harness.context, audioRow);
	harness.Settle();

	UI::ElementHandle sheet = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(sheet));
	const UI::Rect area = harness.context.GetLayout(sheet).content;
	const f32 scale = harness.context.GetContentScale();
	DoubleClickAt(harness.context, static_cast<i32>(area.x + 200.0f), static_cast<i32>(area.y + (22.0f + 9.0f) * scale));
	harness.Settle();

	const UI::ElementHandle box = FindByClass(harness.context, harness.root, EditorTheme::Class::ObjectField, 0);
	ASSERT_TRUE(harness.context.IsAlive(box));
	UI::DragPayload payload;
	payload.type = StringHash("Asset");
	payload.data = Variant(AssetDragData{guid, StringHash("AnimationClip").GetValue()});
	EXPECT_FALSE(harness.context.Events(box).onDragOver(payload));
	payload.data = Variant(AssetDragData{step, TextAsset::GetReflectType()->name.GetValue()});
	ASSERT_TRUE(harness.context.Events(box).onDragOver(payload));
	harness.context.Events(box).onDrop(payload, {});
	harness.Settle();
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Step.txt")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadClip("Walk.anim");
	EXPECT_NE(written.find("\"kind\": \"Audio\""), str::npos);
	EXPECT_NE(written.find(step.ToString()), str::npos);
	AssetCues::UnregisterOwnedBy(&owner);
}

TEST_F(AnimationCurvePanelFixture, MethodPickerOffersCallableMethods) {
	Handle<SceneObject> panelObject = SceneSystem::CreateObject("Panel", _scene);
	ASSERT_TRUE(panelObject.IsValid());
	ASSERT_TRUE(panelObject.Get()->AddComponent<MethodPickProbeComponent>().IsValid());
	const Guid guid = StageClip("Calls.anim", EmptyClipJSON);
	ASSERT_TRUE(guid.IsValid());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	EditorSelection::Select(Handle<Object>(panelObject.Get()));
	harness.Settle();
	EditorSelection::SelectAsset(guid);
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Add Property"));
	UI::ElementHandle methodRow = FindByText(harness.context, harness.context.GetRoot(), "Method");
	ASSERT_TRUE(harness.context.IsAlive(methodRow));
	ClickElement(harness.context, methodRow);

	UI::ElementHandle componentRow =
		FindByText(harness.context, harness.context.GetRoot(), "MethodPickProbeComponent");
	ASSERT_TRUE(harness.context.IsAlive(componentRow));
	ClickElement(harness.context, componentRow);

	EXPECT_FALSE(harness.context.IsAlive(
		FindByText(harness.context, harness.context.GetRoot(), "RingWith")));

	UI::ElementHandle ring = FindByText(harness.context, harness.context.GetRoot(), "Ring");
	ASSERT_TRUE(harness.context.IsAlive(ring));
	ClickElement(harness.context, ring);
	harness.Settle();

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Ring")));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "New Method")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadClip("Calls.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"kind\": \"Method\""), str::npos);
	EXPECT_NE(written.find("\"name\": \"Ring\""), str::npos);
}

TEST_F(AnimationCurvePanelFixture, UnreflectedMethodNameSurvivesAndReported) {
	const str staleClip = std::format(R"({{
"length": 1, "frameRate": 60, "loop": "None", "additiveReferenceTime": 0,
"bakeRootMotionIntoPose": true,
"tracks": [{{"kind": "Method", "interp": "Step", "component": 255, "objectPath": "",
"componentType": "{:016x}", "name": "Vanished",
"times": [0.5], "values": [[0,0,0,0]]}}]
}})",
									  StringHash("MethodPickProbeComponent").GetValue());

	Handle<SceneObject> panelObject = SceneSystem::CreateObject("Panel", _scene);
	ASSERT_TRUE(panelObject.IsValid());
	ASSERT_TRUE(panelObject.Get()->AddComponent<MethodPickProbeComponent>().IsValid());
	const Guid guid = StageClip("Stale.anim", staleClip);
	ASSERT_TRUE(guid.IsValid());

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimationCurvePanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1200, 700};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	EditorSelection::Select(Handle<Object>(panelObject.Get()));
	harness.Settle();
	EditorSelection::SelectAsset(guid);
	harness.Settle();

	UI::ElementHandle staleRow = FindByText(harness.context, harness.root, "Vanished");
	ASSERT_TRUE(harness.context.IsAlive(staleRow));
	ClickElement(harness.context, staleRow);
	harness.Settle();

	UI::ElementHandle picker = FindByClass(harness.context, harness.root, StringHash("slt-dropdown"), 0);
	ASSERT_TRUE(harness.context.IsAlive(picker));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, picker, "Vanished")));

	EXPECT_TRUE(harness.context.IsAlive(
		FindByTextContaining(harness.context, harness.root, "Nothing on this object reflects")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadClip("Stale.anim");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"name\": \"Vanished\""), str::npos);
	EXPECT_EQ(written.find("\"name\": \"Ring\""), str::npos);
}

#endif

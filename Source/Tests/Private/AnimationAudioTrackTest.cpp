#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Asset/TextAsset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

#ifndef SHALLTEAR_SHARED

#include "AnimationClipOps.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/Animator.h"

using namespace Shalltear;

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	struct CueLog {
		std::vector<u64> assets;
		std::vector<u64> objects;
		std::vector<f32> volumes;
	};
	CueLog _cues;

	bool Record(const AssetCue& cue) {
		_cues.assets.push_back(cue.asset.GetInstanceId());
		_cues.objects.push_back(cue.object.GetInstanceId());
		_cues.volumes.push_back(cue.volume);
		return true;
	}

	void RunUpdate(f32 delta) {
		FrameTime clockState;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(delta);
		FrameStageArgs args;
		FrameStages::Run(FrameStage::Update, args);
	}

	class AnimationAudioTrackFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;
		Guid _step;
		TextAsset* _stepAsset = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_animation_audio_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);
			AssetDatabase::Initialize();
			_cues = CueLog{};
			ASSERT_TRUE(AssetCues::Register(TextAsset::GetReflectType(), &Record, &_token));

			ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "Assets" / "Step.txt").generic_string()), str("step")));
			AssetDatabase::ScanProjectDirectory();
			_step = AssetDatabase::LookupGuid(Path("project://Assets/Step.txt"));
			ASSERT_TRUE(_step.IsValid());
			_stepAsset = static_cast<TextAsset*>(AssetDatabase::LoadAssetByGuid(_step));
			ASSERT_NE(_stepAsset, nullptr);

			_scene = SceneSystem::CreateScene("audio-track");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
			if (_stepAsset) {
				AssetDatabase::UnloadAsset(_stepAsset);
			}
			AssetDatabase::Terminate();
			AssetCues::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		AnimationClip* MakeClip(f32 length, AnimationLoopMode loop) {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://audio.anim";
			clip->length = length;
			clip->loop = loop;
			_owned.push_back(clip);
			return clip;
		}

		static void AddCue(AnimationClip& clip, f32 at, const Guid& asset, f32 volume) {
			AnimationClip::Track track;
			track.kind = AnimationTrackKind::Audio;
			track.interp = AnimationInterpolation::Step;
			track.name = "Audio";
			track.times.push_back(at);
			track.values.push_back(Vec4(volume, 0.0f, 0.0f, 0.0f));
			track.assets.push_back(asset);
			clip.tracks.push_back(std::move(track));
		}

		Handle<Animator> MakeAnimator(AnimationClip* clip, Handle<SceneObject>& object) {
			object = SceneSystem::CreateObject("actor", _scene);
			Handle<Animator> animator = object->AddComponent<Animator>();
			animator->SetClip(Handle<AnimationClip>(clip));
			return animator;
		}
	};

} // namespace

TEST_F(AnimationAudioTrackFixture, AudioKeyCuesOncePerCrossing) {
	AnimationClip* clip = MakeClip(0.2f, AnimationLoopMode::Loop);
	AddCue(*clip, 0.1f, _step, 0.25f);
	clip->HoldCueAssets();
	Handle<SceneObject> actor;
	Handle<Animator> animator = MakeAnimator(clip, actor);

	RunUpdate(0.05f);
	EXPECT_TRUE(_cues.assets.empty());
	RunUpdate(0.05f);
	RunUpdate(0.05f);
	ASSERT_EQ(_cues.assets.size(), 1u);
	EXPECT_EQ(_cues.assets[0], _stepAsset->GetInstanceId());
	EXPECT_EQ(_cues.objects[0], actor.GetInstanceId());
	EXPECT_FLOAT_EQ(_cues.volumes[0], 0.25f);

	for (int frame = 0; frame < 12; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_EQ(_cues.assets.size(), 4u);
}

TEST_F(AnimationAudioTrackFixture, EmptyKeySilentClipHoldsCues) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AddCue(*clip, 0.1f, Guid{}, 1.0f);
	AddCue(*clip, 0.2f, AssetDatabase::GenerateGuid(), 1.0f);
	AddCue(*clip, 0.3f, _step, 1.0f);
	clip->HoldCueAssets();
	AssetDatabase::UnloadAsset(_stepAsset);
	_stepAsset = nullptr;
	AssetDatabase::CollectGarbage();
	ASSERT_NE(AssetDatabase::FindLoadedByGuid(_step), nullptr);

	Handle<SceneObject> actor;
	Handle<Animator> animator = MakeAnimator(clip, actor);
	for (int frame = 0; frame < 8; ++frame) {
		RunUpdate(0.05f);
	}
	ASSERT_EQ(_cues.assets.size(), 1u);
	EXPECT_EQ(_cues.assets[0], AssetDatabase::FindLoadedByGuid(_step)->GetInstanceId());
	_stepAsset = static_cast<TextAsset*>(AssetDatabase::LoadAssetByGuid(_step));
}

TEST_F(AnimationAudioTrackFixture, AudioKeyAssetSurvivesCookAndFile) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AddCue(*clip, 0.5f, _step, 0.75f);

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptor(descriptorBytes, pool);
	CookWriter payload(payloadBytes, pool);
	ASSERT_TRUE(clip->Cook(descriptor, payload));
	bool declared = false;
	for (const Guid& reference : descriptor.GetReferences()) {
		declared = declared || reference == _step;
	}
	EXPECT_TRUE(declared);

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	AnimationClip fromCook;
	ASSERT_TRUE(fromCook.LoadCooked(descriptorReader, payloadReader));
	ASSERT_EQ(fromCook.tracks.size(), 1u);
	EXPECT_EQ(fromCook.tracks[0].kind, AnimationTrackKind::Audio);
	ASSERT_EQ(fromCook.tracks[0].assets.size(), 1u);
	EXPECT_EQ(fromCook.tracks[0].assets[0], _step);
	EXPECT_FLOAT_EQ(fromCook.tracks[0].values[0].x, 0.75f);

	const Path file((_root / "round.anim").generic_string());
	ASSERT_TRUE(clip->Export(file));
	AnimationClip fromFile;
	ASSERT_TRUE(fromFile.Import(file));
	ASSERT_EQ(fromFile.tracks.size(), 1u);
	EXPECT_EQ(fromFile.tracks[0].kind, AnimationTrackKind::Audio);
	ASSERT_EQ(fromFile.tracks[0].assets.size(), 1u);
	EXPECT_EQ(fromFile.tracks[0].assets[0], _step);
	EXPECT_FLOAT_EQ(fromFile.tracks[0].values[0].x, 0.75f);

	const str broken = FileSystem::ReadTextFile(file, MemTag::Asset);
	const size_t at = broken.find("\"assets\"");
	ASSERT_NE(at, str::npos);
	str truncated = broken.substr(0, at) + "\"assets\": [],\"ignored\": 0" + broken.substr(broken.find(']', at) + 1);
	ASSERT_TRUE(FileSystem::WriteTextFile(file, truncated));
	AnimationClip refused;
	EXPECT_FALSE(refused.Import(file));
}

TEST_F(AnimationAudioTrackFixture, OpsKeepOneAssetToKey) {
	AnimationClip* clip = MakeClip(1.0f, AnimationLoopMode::None);
	AnimationClipOps::TrackAddress address;
	address.kind = AnimationTrackKind::Audio;
	address.name = "Audio";
	const i32 track = AnimationClipOps::AddTrack(*clip, address, AnimationInterpolation::Step);
	ASSERT_GE(track, 0);
	const i32 first = AnimationClipOps::AddKey(*clip, track, 0.2f);
	const i32 second = AnimationClipOps::AddKey(*clip, track, 0.6f);
	ASSERT_EQ(first, 0);
	ASSERT_EQ(second, 1);
	const AnimationClip::Track& curve = clip->tracks[static_cast<size_t>(track)];
	ASSERT_EQ(curve.assets.size(), 2u);
	EXPECT_FALSE(curve.assets[0].IsValid());
	EXPECT_FLOAT_EQ(curve.values[0].x, 1.0f);

	ASSERT_TRUE(AnimationClipOps::SetKeyAsset(*clip, track, 0, _step));
	const i32 moved = AnimationClipOps::MoveKey(*clip, track, 0, 0.8f);
	ASSERT_EQ(moved, 1);
	EXPECT_FALSE(curve.assets[0].IsValid());
	EXPECT_EQ(curve.assets[1], _step);

	AnimationClip sliced;
	AnimationClipOps::SliceRange(*clip, 0.5f, 1.0f, sliced);
	ASSERT_EQ(sliced.tracks.size(), 1u);
	EXPECT_EQ(sliced.tracks[0].times.size(), 2u);
	ASSERT_EQ(sliced.tracks[0].assets.size(), 2u);
	EXPECT_EQ(sliced.tracks[0].assets[1], _step);

	ASSERT_TRUE(AnimationClipOps::RemoveKey(*clip, track, 1));
	ASSERT_EQ(curve.assets.size(), 1u);
	EXPECT_FALSE(curve.assets[0].IsValid());
	EXPECT_FALSE(AnimationClipOps::SetKeyAsset(*clip, track, 5, _step));
}

#endif

#pragma once

#ifndef SHALLTEAR_SHARED

#include "AudioClipAccess.h"
#include "AudioClipData.h"
#include "AudioImportOps.h"
#include "AudioPackage.h"
#include "AudioSystem.h"

#include "Shalltear/Audio/AudioClip.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

SLT_DECLARE_LINKED_PACKAGE(Audio)

namespace AudioTesting {

	using namespace Shalltear;
	using namespace Shalltear::AudioImpl;

	class LivePackageFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		char _token = 0;
		std::vector<AudioClip*> _clips;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_live_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, ProjectSettings());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Audio, &_token);
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ShutdownPackage();
			for (AudioClip* clip : _clips) {
				Delete(clip);
			}
			_clips.clear();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		virtual str ProjectSettings() const { return str("[Audio]\ndevice = None\n"); }

		AudioClip* MakeClip(u64 frames, u32 channels, f32 value, AudioCompressionFormat format = AudioCompressionFormat::PCM,
							AudioClipLoadType loadType = AudioClipLoadType::DecompressOnLoad, u32 rate = 48000) {
			DecodedAudio decoded;
			decoded.layout = MakeStandardLayout(channels);
			decoded.sampleRate = rate;
			decoded.samples.assign(static_cast<size_t>(frames * channels), value);
			AudioImportSettings settings;
			settings.compressionFormat = format;
			settings.loadType = loadType;
			AudioClip* clip = New<AudioClip>(MemTag::Asset);
			str error;
			if (!BuildClip(decoded, settings, *clip, error)) {
				ADD_FAILURE() << "test clip didn't build: " << error;
			}
			_clips.push_back(clip);
			return clip;
		}

		void Forget(AudioClip* clip) {
			std::erase(_clips, clip);
		}

		static Handle<AudioResource> ResourceOf(AudioClip* clip) { return Handle<AudioResource>(clip); }

		static u32 ReferencesOf(const AudioClip* clip) {
			const AudioClipData* data = AudioClipAccess::GetPlayback(*clip);
			return data ? data->references : 0;
		}

		static f32 LastBlockPeak() {
			return std::bit_cast<f32>(GetAudioSystem().GetRenderer().GetCounters().lastBlockPeak.load(std::memory_order_relaxed));
		}

		static u64 BlocksRendered() { return GetAudioSystem().GetRenderer().GetCounters().blocks.load(std::memory_order_relaxed); }

		template <typename Condition>
		static bool WaitFor(Condition&& condition, std::chrono::milliseconds deadline = std::chrono::milliseconds(3000)) {
			const auto giveUp = std::chrono::steady_clock::now() + deadline;
			while (!condition()) {
				if (std::chrono::steady_clock::now() > giveUp) {
					return condition();
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return true;
		}

		static bool WaitForBlocks(u64 count) {
			const u64 target = BlocksRendered() + count;
			return WaitFor([target]() { return BlocksRendered() >= target; });
		}

		static u32 ClaimedVoices() {
			GetAudioSystem().DrainEvents();
			return GetAudioSystem().GetVoices().CountClaimed();
		}

		static void RunFrame(f32 scale = 1.0f, bool gameplay = true, f32 seconds = 1.0f / 60.0f) {
			FrameTime clockState;
			clockState.maximumDeltaTime = 1000.0f;
			clockState.timeScale = scale;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(seconds);
			FrameStageArgs args;
			if (gameplay) {
				FrameStages::Run(FrameStage::Update, args);
				FrameStages::Run(FrameStage::LateUpdate, args);
			}
			FrameStages::Run(FrameStage::EndFrame, args);
		}

		static void RunFrameWithSteps(u32 steps, f32 fixedDelta, f32 seconds) {
			FrameTime clockState;
			clockState.maximumDeltaTime = 1000.0f;
			clockState.fixedDeltaTime = fixedDelta;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(seconds);
			FrameStageArgs args;
			for (u32 step = 0; step < steps; ++step) {
				FrameStages::Run(FrameStage::FixedUpdate, args);
			}
			FrameStages::Run(FrameStage::Update, args);
			FrameStages::Run(FrameStage::LateUpdate, args);
			FrameStages::Run(FrameStage::EndFrame, args);
		}

		static u64 PositionOf(AudioVoice voice) { return Audio::GetTimeSamples(voice); }

		static bool IsAdvancing(AudioVoice voice) {
			const u64 before = PositionOf(voice);
			WaitForBlocks(4);
			return PositionOf(voice) != before;
		}
	};

} // namespace AudioTesting

#endif

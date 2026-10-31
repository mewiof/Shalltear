#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioOutputs.h"
#include "AudioPlayback.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

using namespace AudioTesting;

namespace {

	class AudioPlaybackFixture : public LivePackageFixture {};

	class AudioLogSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "audio-log-sink-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (_armed) {
				_messages.push_back(str(event.message));
			}
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				count += message.find(needle) != str::npos ? 1u : 0u;
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

	class AudioLogFixture : public AudioPlaybackFixture {
	protected:
		void SetUp() override {
			_ownsJobSystem = JobSystem::GetWorkerCount() == 0;
			if (_ownsJobSystem) {
				JobSystem::Initialize();
			}
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			LogManager::AddSink(_sink);
			AudioPlaybackFixture::SetUp();
			SLT_LOG_WARN("Audio", "{}", AudioLogSink::SENTINEL);
			ASSERT_TRUE(WaitFor([this]() { return _sink->Armed(); }));
		}

		void TearDown() override {
			AudioPlaybackFixture::TearDown();
			LogManager::Terminate();
			if (_ownsJobSystem) {
				JobSystem::Terminate();
			}
		}

		bool Settle(std::string_view marker) {
			SLT_LOG_WARN("Audio", "{}", marker);
			return WaitFor([this, marker]() { return _sink->CountMatching(marker) == 1; });
		}

		u32 Saying(std::string_view needle) const { return _sink->CountMatching(needle); }

		std::shared_ptr<AudioLogSink> _sink = std::make_shared<AudioLogSink>();
		bool _ownsJobSystem = false;
	};

	class NotAClip : public Object {};

	AudioPlayParams Looping() {
		AudioPlayParams params;
		params.loop = true;
		return params;
	}

} // namespace

TEST_F(AudioPlaybackFixture, VoiceFreesSlotAtClipEnd) {
	AudioClip* clip = MakeClip(480, 1, 0.5f);
	const u32 held = ReferencesOf(clip);

	const AudioVoice voice = Audio::Play(ResourceOf(clip));
	ASSERT_TRUE(voice.IsValid());
	EXPECT_TRUE(Audio::IsPlaying(voice));
	EXPECT_EQ(ReferencesOf(clip), held + 1);
	EXPECT_EQ(ClaimedVoices(), 1u);

	ASSERT_TRUE(WaitFor([&]() { return !Audio::IsPlaying(voice); }));
	EXPECT_EQ(ReferencesOf(clip), held);
	EXPECT_EQ(ClaimedVoices(), 0u);
}

TEST_F(AudioPlaybackFixture, StopFreesSlotAfterFade) {
	AudioClip* clip = MakeClip(4800, 2, 0.25f);
	const AudioVoice voice = Audio::Play(ResourceOf(clip), Looping());
	ASSERT_TRUE(voice.IsValid());

	ASSERT_TRUE(WaitForBlocks(24));
	EXPECT_TRUE(Audio::IsPlaying(voice));

	Audio::Stop(voice);
	EXPECT_FALSE(Audio::IsPlaying(voice));
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
}

TEST_F(AudioPlaybackFixture, DestroyedClipKeepsSamplesForVoice) {
	GetAudioSystem().SetListenerPresent(true);
	const size_t before = MemoryManager::GetBytesForTag(MemTag::Audio);
	AudioClip* clip = MakeClip(48000, 2, 0.5f);
	const size_t withClip = MemoryManager::GetBytesForTag(MemTag::Audio);
	ASSERT_GT(withClip - before, static_cast<size_t>(48000) * 2 * sizeof(f32));

	const AudioVoice voice = Audio::Play(ResourceOf(clip), Looping());
	ASSERT_TRUE(voice.IsValid());
	Forget(clip);
	Delete(clip);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), withClip);

	ASSERT_TRUE(WaitForBlocks(8));
	EXPECT_TRUE(Audio::IsPlaying(voice));
	EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() > 0.0f; }));

	Audio::Stop(voice);
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), before);
}

TEST_F(AudioPlaybackFixture, FullPoolRefusesEqualPlay) {
	AudioClip* clip = MakeClip(4800, 1, 0.1f);
	const u32 limit = GetAudioSystem().GetVoices().GetLimit();
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < limit; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), Looping()));
		ASSERT_TRUE(voices.back().IsValid()) << "voice " << index;
	}
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), Looping()).IsValid());
	EXPECT_EQ(GetAudioSystem().GetBudgetCounts().refusedByPool, 1u);

	Audio::Stop(voices[17]);
	EXPECT_TRUE(Audio::Play(ResourceOf(clip), Looping()).IsValid());
	EXPECT_EQ(GetAudioSystem().GetVoices().CountLive(), limit);
}

TEST_F(AudioPlaybackFixture, EndedVoiceCallSkipsNextOccupant) {
	AudioClip* blip = MakeClip(256, 1, 0.1f);
	AudioClip* loop = MakeClip(4800, 1, 0.1f);
	const AudioVoice ended = Audio::Play(ResourceOf(blip));
	ASSERT_TRUE(ended.IsValid());
	ASSERT_TRUE(WaitFor([&]() { return !Audio::IsPlaying(ended) && ClaimedVoices() == 0; }));

	const u32 endedSlot = AudioVoiceAccess::GetSlot(ended);
	const u32 limit = GetAudioSystem().GetVoices().GetLimit();
	std::vector<AudioVoice> voices;
	for (u32 round = 0; round < 4; ++round) {
		voices.clear();
		for (u32 index = 0; index < limit; ++index) {
			voices.push_back(Audio::Play(ResourceOf(loop), Looping()));
			ASSERT_TRUE(voices.back().IsValid());
		}
		if (std::any_of(voices.begin(), voices.end(), [&](AudioVoice voice) { return AudioVoiceAccess::GetSlot(voice) == endedSlot; })) {
			break;
		}
		for (const AudioVoice& voice : voices) {
			Audio::Stop(voice);
		}
		ASSERT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
	}
	ASSERT_TRUE(std::any_of(voices.begin(), voices.end(), [&](AudioVoice voice) { return AudioVoiceAccess::GetSlot(voice) == endedSlot; }));

	Audio::Pause(ended);
	Audio::Stop(ended);
	Audio::SetTimeSamples(ended, 100);
	EXPECT_FALSE(Audio::IsPlaying(ended));
	EXPECT_EQ(Audio::GetTimeSamples(ended), 0u);

	ASSERT_TRUE(WaitForBlocks(4));
	u32 playing = 0;
	for (const AudioVoice& voice : voices) {
		playing += Audio::IsPlaying(voice) ? 1u : 0u;
	}
	EXPECT_EQ(playing, limit);
	EXPECT_EQ(ClaimedVoices(), limit);
}

TEST_F(AudioPlaybackFixture, PauseHoldsUnPauseResumes) {
	AudioClip* clip = MakeClip(480000, 1, 0.1f);
	const AudioVoice voice = Audio::Play(ResourceOf(clip));
	ASSERT_TRUE(voice.IsValid());
	ASSERT_TRUE(WaitFor([&]() { return Audio::GetTimeSamples(voice) > 2000; }));

	Audio::Pause(voice);
	EXPECT_FALSE(Audio::IsPlaying(voice));
	u64 held = 0;
	ASSERT_TRUE(WaitFor([&]() {
		const u64 earlier = Audio::GetTimeSamples(voice);
		WaitForBlocks(2);
		held = Audio::GetTimeSamples(voice);
		return held == earlier;
	}));
	ASSERT_TRUE(WaitForBlocks(8));
	EXPECT_EQ(Audio::GetTimeSamples(voice), held);

	Audio::UnPause(voice);
	EXPECT_TRUE(Audio::IsPlaying(voice));
	ASSERT_TRUE(WaitFor([&]() { return Audio::GetTimeSamples(voice) > held + 1000; }));
	EXPECT_LT(Audio::GetTimeSamples(voice), held + 48000);
}

TEST_F(AudioPlaybackFixture, TimeSamplesAnswersAskedValue) {
	AudioClip* clip = MakeClip(480000, 1, 0.1f);
	AudioPlayParams params;
	params.startSample = 12345;
	const AudioVoice voice = Audio::Play(ResourceOf(clip), params);
	ASSERT_TRUE(voice.IsValid());
	EXPECT_EQ(Audio::GetTimeSamples(voice), 12345u);
	EXPECT_TRUE(WaitFor([&]() { return Audio::GetTimeSamples(voice) > 12345u + 256; }));

	Audio::SetTimeSamples(voice, 200000);
	EXPECT_EQ(Audio::GetTimeSamples(voice), 200000u);
	EXPECT_TRUE(WaitFor([&]() { return Audio::GetTimeSamples(voice) > 200000u + 256; }));

	Audio::SetTimeSamples(voice, 999999999);
	EXPECT_EQ(Audio::GetTimeSamples(voice), 480000u);
	EXPECT_TRUE(WaitFor([&]() { return !Audio::IsPlaying(voice); }));
}

TEST_F(AudioPlaybackFixture, StopGameVoicesStopsGameOnly) {
	AudioClip* loop = MakeClip(4800, 1, 0.1f);
	AudioClip* blip = MakeClip(128, 1, 0.1f);
	const AudioVoice first = Audio::Play(ResourceOf(loop), Looping());
	const AudioVoice second = Audio::Play(ResourceOf(loop), Looping());
	const AudioVoice held = Audio::Play(ResourceOf(loop), Looping());
	const AudioVoice ended = Audio::Play(ResourceOf(blip));
	Audio::Pause(held);
	ASSERT_TRUE(WaitFor([&]() { return !Audio::IsPlaying(ended); }));
	const AudioVoice stopping = Audio::Play(ResourceOf(loop), Looping());
	Audio::Stop(stopping);

	EXPECT_EQ(StopGameVoices(), 3u);
	for (const AudioVoice& voice : {first, second, held}) {
		EXPECT_FALSE(Audio::IsPlaying(voice));
	}
	Audio::UnPause(held);
	EXPECT_FALSE(Audio::IsPlaying(held));
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
	EXPECT_EQ(StopGameVoices(), 0u);
}

TEST_F(AudioPlaybackFixture, NoListenerMeansSilence) {
	AudioClip* clip = MakeClip(4800, 1, 0.5f);
	ASSERT_TRUE(Audio::Play(ResourceOf(clip), Looping()).IsValid());
	ASSERT_TRUE(WaitForBlocks(8));
	EXPECT_EQ(LastBlockPeak(), 0.0f);

	GetAudioSystem().SetListenerPresent(true);
	const f32 expected = 0.5f * std::sqrt(0.5f);
	EXPECT_TRUE(WaitFor([&]() { return std::fabs(LastBlockPeak() - expected) < 1e-6f; })) << LastBlockPeak();

	GetAudioSystem().SetListenerPresent(false);
	EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() == 0.0f; }));
}

TEST_F(AudioLogFixture, GatedSoundReportedUnheard) {
	constexpr std::string_view UNHEARD = "no Audio Listener is enabled";
	AudioClip* clip = MakeClip(4800, 1, 0.5f);
	AudioPlayParams params = Looping();
	params.output = GetDeviceOutput();
	ASSERT_TRUE(params.output.IsValid());
	const AudioVoice preview = Audio::Play(ResourceOf(clip), params);
	ASSERT_TRUE(preview.IsValid());
	ASSERT_TRUE(WaitFor([]() { return LastBlockPeak() > 0.0f; }));
	ASSERT_TRUE(Settle("audio-log-after-the-preview"));
	EXPECT_EQ(Saying(UNHEARD), 0u);

	Audio::Stop(preview);
	ASSERT_TRUE(Audio::Play(ResourceOf(clip), Looping()).IsValid());
	ASSERT_TRUE(Audio::Play(ResourceOf(clip), Looping()).IsValid());
	ASSERT_TRUE(Settle("audio-log-after-the-game"));
	EXPECT_EQ(Saying(UNHEARD), 1u);
}

TEST_F(AudioPlaybackFixture, ClockCountsAtOutputRate) {
	EXPECT_EQ(Audio::GetOutputSampleRate(), 48000u);
	ASSERT_TRUE(WaitForBlocks(4));
	const AudioTime first = Audio::GetTime();
	AudioTime last = first;
	u32 backwards = 0;
	const auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(400)) {
		const AudioTime now = Audio::GetTime();
		backwards += now < last ? 1u : 0u;
		last = now;
	}
	const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
	EXPECT_EQ(backwards, 0u);
	const f64 counted = static_cast<f64>(last.frames - first.frames);
	EXPECT_NEAR(counted / seconds, 48000.0, 12000.0) << counted << " frames in " << seconds << " s";
}

TEST_F(AudioPlaybackFixture, HandleThatNamesNoClipPlaysNothing) {
	EXPECT_FALSE(Audio::Play(Handle<AudioResource>{}).IsValid());
	NotAClip* other = New<NotAClip>(MemTag::Core);
	const Handle<AudioResource> wrong = std::bit_cast<Handle<AudioResource>>(Handle<Object>(other));
	EXPECT_FALSE(Audio::Play(wrong).IsValid());
	Delete(other);
	EXPECT_EQ(ClaimedVoices(), 0u);
}

#endif

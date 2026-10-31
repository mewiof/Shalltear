#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioRing.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioStream;

		class AudioStreamer {
		public:
			static constexpr u32 POLL_MILLISECONDS = 4;

			AudioStreamer() = default;
			~AudioStreamer() { Stop(); }
			AudioStreamer(const AudioStreamer&) = delete;
			AudioStreamer& operator=(const AudioStreamer&) = delete;

			void Start(u32 capacity, bool threaded);

			void Stop();

			bool IsStarted() const { return _streams != nullptr; }
			u32 GetCapacity() const { return _capacity; }

			bool Add(AudioStream* stream);

			void Wake();

			u64 GetFailures() const { return _failures.load(std::memory_order_relaxed); }

			void Pump() [[clang::nonallocating]];
		private:
			void Run();

			AudioRing<AudioStream*> _adds;
			AudioStream** _streams = nullptr;
			u32 _count = 0;
			u32 _capacity = 0;
			std::atomic<u64> _failures{0};

			std::thread _thread;
			std::mutex _mutex;
			std::condition_variable _wake;
			bool _signaled = false;
			bool _quit = false;
		};

	} // namespace AudioImpl

} // namespace Shalltear

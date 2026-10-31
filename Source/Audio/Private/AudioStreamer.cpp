#include "AudioStreamer.h"

#include "AudioStream.h"

#include "Shalltear/Core/Memory.h"

#include <chrono>

namespace Shalltear {

	namespace AudioImpl {

		void AudioStreamer::Start(u32 capacity, bool threaded) {
			if (_streams) {
				return;
			}
			_capacity = capacity > 0 ? capacity : 1;
			_streams = static_cast<AudioStream**>(Allocate(sizeof(AudioStream*) * _capacity, MemTag::Audio, 64));
			_count = 0;
			_adds.Reserve(_capacity);
			_failures.store(0, std::memory_order_relaxed);
			_signaled = false;
			_quit = false;
			if (threaded) {
				_thread = std::thread([this]() { Run(); });
			}
		}

		void AudioStreamer::Stop() {
			if (!_streams) {
				return;
			}
			if (_thread.joinable()) {
				{
					std::lock_guard lock(_mutex);
					_quit = true;
					_signaled = true;
				}
				_wake.notify_one();
				_thread.join();
			}
			AudioStream* added = nullptr;
			while (_adds.TryPop(added)) {
				added->released.store(true, std::memory_order_release);
			}
			for (u32 index = 0; index < _count; ++index) {
				_streams[index]->released.store(true, std::memory_order_release);
			}
			Free(_streams);
			_streams = nullptr;
			_count = 0;
			_capacity = 0;
			_adds.Release();
		}

		bool AudioStreamer::Add(AudioStream* stream) {
			return stream && _streams && _adds.TryPush(stream);
		}

		void AudioStreamer::Wake() {
			if (!_thread.joinable()) {
				return;
			}
			{
				std::lock_guard lock(_mutex);
				_signaled = true;
			}
			_wake.notify_one();
		}

		void AudioStreamer::Pump() {
			AudioStream* added = nullptr;
			while (_adds.TryPop(added)) {
				if (_count < _capacity) {
					_streams[_count++] = added;
				}
			}

			u32 kept = 0;
			for (u32 index = 0; index < _count; ++index) {
				AudioStream* stream = _streams[index];
				if (stream->retiring.load(std::memory_order_acquire)) {
					stream->released.store(true, std::memory_order_release);
					continue;
				}
				_streams[kept++] = stream;
			}
			_count = kept;

			for (bool made = true; made;) {
				made = false;
				for (u32 index = 0; index < _count; ++index) {
					made = ProduceChunk(*_streams[index], _failures) || made;
				}
			}
		}

		void AudioStreamer::Run() {
			for (;;) {
				Pump();
				std::unique_lock lock(_mutex);
				if (!_signaled && !_quit) {
					if (_count > 0) {
						_wake.wait_for(lock, std::chrono::milliseconds(POLL_MILLISECONDS));
					} else {
						_wake.wait(lock);
					}
				}
				_signaled = false;
				if (_quit) {
					return;
				}
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear

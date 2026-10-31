#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"

#include "Graphics/ShaderCompiler.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace Shalltear {

	namespace {

		constexpr u32 COMPILE_THREADS = 2;

		enum class JobState : u8 {
			Queued,
			Running,
			Done
		};

		struct Job {
			ShaderCompileTicket ticket = 0;
			Path path;
			Vector<str> keywords{GetMemoryResource(MemTag::Graphics)};
			ShaderAsset* shader = nullptr;
			JobState state = JobState::Queued;
			bool succeeded = false;
			str messages;
			bool cancelled = false;
		};

		struct QueueState {
			std::mutex mutex;
			std::condition_variable wake;
			std::deque<Job*> queued;
			std::vector<Job*> jobs;
			std::vector<std::thread> threads;
			bool stopping = false;
			ShaderCompileTicket nextTicket = 1;
			u64 submitted = 0;
			u64 finished = 0;
		};

		QueueState* _queue = nullptr;

		void CompileThread(QueueState* queue) {
			ShaderCompiler::MarkCompileThread();
			for (;;) {
				Job* job = nullptr;
				{
					std::unique_lock lock(queue->mutex);
					queue->wake.wait(lock, [queue] { return queue->stopping || !queue->queued.empty(); });
					if (queue->stopping) {
						break;
					}
					job = queue->queued.front();
					queue->queued.pop_front();
					job->state = JobState::Running;
				}
				bool succeeded = false;
				{
					ShaderCompiler::ScopedMessages said(job->messages);
					succeeded = job->shader->Import(job->path);
					if (succeeded && !job->keywords.empty()) {
						job->shader->GetOrCompileVariant({job->keywords.data(), job->keywords.size()});
					}
				}
				{
					std::lock_guard lock(queue->mutex);
					job->succeeded = succeeded;
					job->state = JobState::Done;
					++queue->finished;
				}
			}
			ShaderCompiler::ReleaseThreadCompiler();
		}

		QueueState& Started() {
			if (!_queue) {
				_queue = New<QueueState>(MemTag::Graphics);
			}
			if (_queue->threads.empty()) {
				_queue->threads.reserve(COMPILE_THREADS);
				for (u32 index = 0; index < COMPILE_THREADS; ++index) {
					_queue->threads.emplace_back(CompileThread, _queue);
				}
			}
			return *_queue;
		}

		void FreeJob(Job* job) {
			if (job->shader) {
				Delete(job->shader);
			}
			Delete(job);
		}

		void ReapLocked(QueueState& queue) {
			std::erase_if(queue.jobs, [](Job* job) {
				if (job->cancelled && job->state == JobState::Done) {
					FreeJob(job);
					return true;
				}
				return false;
			});
		}

		Job* FindLocked(QueueState& queue, ShaderCompileTicket ticket) {
			for (Job* job : queue.jobs) {
				if (job->ticket == ticket && !job->cancelled) {
					return job;
				}
			}
			return nullptr;
		}

	} // anonymous namespace

	namespace ShaderCompileQueue {

		ShaderCompileTicket Submit(const Path& virtualOrPhysical, std::span<const str> keywords) {
			QueueState& queue = Started();
			Job* job = New<Job>(MemTag::Graphics);
			job->path = virtualOrPhysical;
			job->keywords.assign(keywords.begin(), keywords.end());
			job->shader = New<ShaderAsset>(MemTag::Asset);
			{
				std::lock_guard lock(queue.mutex);
				ReapLocked(queue);
				job->ticket = queue.nextTicket++;
				queue.jobs.push_back(job);
				queue.queued.push_back(job);
				++queue.submitted;
			}
			queue.wake.notify_one();
			return job->ticket;
		}

		ShaderCompileStatus Collect(ShaderCompileTicket ticket, ShaderAsset*& out, str* messages) {
			out = nullptr;
			if (!_queue || ticket == 0) {
				return ShaderCompileStatus::Unknown;
			}
			QueueState& queue = *_queue;
			Job* job = nullptr;
			{
				std::lock_guard lock(queue.mutex);
				ReapLocked(queue);
				job = FindLocked(queue, ticket);
				if (!job) {
					return ShaderCompileStatus::Unknown;
				}
				if (job->state != JobState::Done) {
					return ShaderCompileStatus::Pending;
				}
				std::erase(queue.jobs, job);
			}
			const bool succeeded = job->succeeded;
			if (succeeded) {
				out = job->shader;
				job->shader = nullptr;
			}
			if (messages) {
				*messages = std::move(job->messages);
			}
			FreeJob(job);
			return succeeded ? ShaderCompileStatus::Succeeded : ShaderCompileStatus::Failed;
		}

		void Cancel(ShaderCompileTicket ticket) {
			if (!_queue || ticket == 0) {
				return;
			}
			QueueState& queue = *_queue;
			Job* freed = nullptr;
			{
				std::lock_guard lock(queue.mutex);
				ReapLocked(queue);
				Job* job = FindLocked(queue, ticket);
				if (!job) {
					return;
				}
				switch (job->state) {
				case JobState::Queued:
					std::erase(queue.queued, job);
					std::erase(queue.jobs, job);
					freed = job;
					break;
				case JobState::Running:
					job->cancelled = true;
					break;
				case JobState::Done:
					std::erase(queue.jobs, job);
					freed = job;
					break;
				}
			}
			if (freed) {
				FreeJob(freed);
			}
		}

		ShaderCompileStats GetStats() {
			ShaderCompileStats stats;
			if (_queue) {
				std::lock_guard lock(_queue->mutex);
				ReapLocked(*_queue);
				stats.submitted = _queue->submitted;
				stats.finished = _queue->finished;
			}
			ShaderCompiler::GetCompileCounts(stats.compilesOnCompileThreads, stats.compilesElsewhere);
			return stats;
		}

		void Terminate() {
			if (!_queue) {
				return;
			}
			{
				std::lock_guard lock(_queue->mutex);
				_queue->stopping = true;
			}
			_queue->wake.notify_all();
			for (std::thread& thread : _queue->threads) {
				thread.join();
			}
			size_t uncollected = 0;
			for (Job* job : _queue->jobs) {
				uncollected += job->cancelled ? 0 : 1;
				FreeJob(job);
			}
			if (uncollected > 0) {
				SLT_LOG_INFO("Graphics", "ShaderCompileQueue: {} compile(s) nobody collected were dropped at shutdown", uncollected);
			}
			Delete(_queue);
			_queue = nullptr;
		}

	} // namespace ShaderCompileQueue

} // namespace Shalltear

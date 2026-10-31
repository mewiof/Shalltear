#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"

#include "Module/EngineStateTable.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace Shalltear {

	constexpr u32 MAX_JOBS = 16384;
	constexpr u32 MAX_EDGES = 65536;
	constexpr size_t JOB_PAYLOAD_SIZE = 64;

	struct JobEdge;

	struct JobNode {
		void (*function)(void*) = nullptr;
		alignas(16) u8 payload[JOB_PAYLOAD_SIZE];

		std::mutex mutex;
		std::atomic<u32> generation{1};
		bool isCompleted = true;

		JobEdge* continuationList = nullptr;
		std::atomic<i32> unfinishedDependencies{0};

		u32 index = 0;
		JobType type = JobType::Normal;
	};

	struct JobEdge {
		JobNode* waitingJob = nullptr;
		JobEdge* next = nullptr;
		u32 index = 0;
	};

	class WorkerQueue {
	public:
		void Push(JobNode* job) {
			std::lock_guard lock(_mutex);
			_queue.push_back(job);
			_cv.notify_one();
		}

		JobNode* Pop() {
			std::lock_guard lock(_mutex);
			if (_queue.empty())
				return nullptr;
			JobNode* job = _queue.back();
			_queue.pop_back();
			return job;
		}

		JobNode* Steal() {
			std::unique_lock lock(_mutex, std::try_to_lock);
			if (!lock.owns_lock() || _queue.empty())
				return nullptr;
			JobNode* job = _queue.front();
			_queue.pop_front();
			return job;
		}

		void WaitAndPop(JobNode*& outJob, std::atomic<bool>& isRunning) {
			std::unique_lock lock(_mutex);
			_cv.wait(lock, [&] { return !_queue.empty() || !isRunning.load(std::memory_order_relaxed); });
			if (!_queue.empty()) {
				outJob = _queue.back();
				_queue.pop_back();
			} else {
				outJob = nullptr;
			}
		}

		void NotifyAll() {
			std::lock_guard lock(_mutex);
			_cv.notify_all();
		}
	private:
		std::mutex _mutex;
		std::condition_variable _cv;
		std::deque<JobNode*> _queue;
	};

	struct JobSystemData {
		JobNode nodes[MAX_JOBS];
		JobEdge edges[MAX_EDGES];

		std::vector<u32> freeNodes;
		std::mutex freeNodesMutex;

		std::vector<u32> freeEdges;
		std::mutex freeEdgesMutex;

		std::vector<std::thread> workerThreads;
		std::vector<std::unique_ptr<WorkerQueue>> queues;

		std::vector<std::thread> longRunningThreads;
		std::mutex longRunningMutex;

		std::atomic<bool> isRunning{false};
		std::atomic<u32> roundRobinCounter{0};
	};

	static JobSystemData* _data = nullptr;

	thread_local u32 _workerIndex = 0xFFFFFFFF;

	namespace ModuleInternal {

		void CaptureJobSystemEngineState(EngineStateTable& table) {
			table.jobSystem = _data;
		}

		void BindJobSystemEngineState(const EngineStateTable& table) {
			_data = static_cast<JobSystemData*>(table.jobSystem);
		}

	} // namespace ModuleInternal

	static JobNode* AllocateNode() {
		std::lock_guard lock(_data->freeNodesMutex);
		SLT_CORE_ASSERT_MSG(!_data->freeNodes.empty(), "JobSystem heavily bottlenecked: Out of nodes!");
		u32 index = _data->freeNodes.back();
		_data->freeNodes.pop_back();

		JobNode* node = &_data->nodes[index];
		node->isCompleted = false;
		node->continuationList = nullptr;
		node->type = JobType::Normal;
		return node;
	}

	static void FreeNode(JobNode* node) {
		node->generation.fetch_add(1, std::memory_order_release);
		std::lock_guard lock(_data->freeNodesMutex);
		_data->freeNodes.push_back(node->index);
	}

	static JobEdge* AllocateEdge() {
		std::lock_guard lock(_data->freeEdgesMutex);
		SLT_CORE_ASSERT_MSG(!_data->freeEdges.empty(), "JobSystem heavily bottlenecked: Out of edges!");
		u32 index = _data->freeEdges.back();
		_data->freeEdges.pop_back();
		return &_data->edges[index];
	}

	static void FreeEdge(JobEdge* edge) {
		std::lock_guard lock(_data->freeEdgesMutex);
		_data->freeEdges.push_back(edge->index);
	}

	static void PushToWorkerQueue(JobNode* node) {
		u32 index = _data->roundRobinCounter.fetch_add(1, std::memory_order_relaxed) % _data->queues.size();
		_data->queues[index]->Push(node);
	}

	static void DispatchNode(JobNode* node);
	static void ExecuteNode(JobNode* node);

	static void DispatchNode(JobNode* node) {
		if (node->type == JobType::LongRunning) {
			std::lock_guard lock(_data->longRunningMutex);
			_data->longRunningThreads.emplace_back([node]() {
				ExecuteNode(node);
			});
		} else {
			PushToWorkerQueue(node);
		}
	}

	static JobNode* Resolve(JobHandle handle) {
		if (!handle.IsValid() || handle.index >= MAX_JOBS)
			return nullptr;

		JobNode* node = &_data->nodes[handle.index];
		if (node->generation.load(std::memory_order_acquire) == handle.generation) {
			return node;
		}
		return nullptr;
	}

	static void ExecuteNode(JobNode* node) {
		if (node->function) {
			try {
				node->function(node->payload);
			} catch (const std::exception& e) {
				SLT_LOG_ERROR("Core", "Job threw an exception: {}", e.what());
			} catch (...) {
				SLT_LOG_ERROR("Core", "Job threw an unknown exception!");
			}
		}

		JobEdge* conts = nullptr;
		{
			std::lock_guard lock(node->mutex);
			node->isCompleted = true;
			conts = node->continuationList;
			node->continuationList = nullptr;
		}

		while (conts) {
			JobEdge* edge = conts;
			conts = edge->next;

			JobNode* waitingJob = edge->waitingJob;
			if (waitingJob->unfinishedDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				DispatchNode(waitingJob);
			}

			FreeEdge(edge);
		}

		FreeNode(node);
	}

	void JobSystem::Initialize() {
		SLT_CORE_ASSERT_MSG(_data == nullptr, "JobSystem double initialization detected!");
		_data = new JobSystemData();

		for (u32 i = 0; i < MAX_JOBS; ++i) {
			_data->nodes[i].index = i;
			_data->freeNodes.push_back(MAX_JOBS - 1 - i);
		}

		for (u32 i = 0; i < MAX_EDGES; ++i) {
			_data->edges[i].index = i;
			_data->freeEdges.push_back(MAX_EDGES - 1 - i);
		}

		u32 numThreads = std::thread::hardware_concurrency();
		if (numThreads == 0)
			numThreads = 4;
		u32 workerCount = std::max(1u, numThreads - 1);

		_data->isRunning.store(true, std::memory_order_release);

		for (u32 i = 0; i < workerCount; ++i) {
			_data->queues.push_back(std::make_unique<WorkerQueue>());
		}

		for (u32 i = 0; i < workerCount; ++i) {
			_data->workerThreads.emplace_back([i, workerCount]() {
				_workerIndex = i;

				while (_data->isRunning.load(std::memory_order_relaxed)) {
					JobNode* job = _data->queues[i]->Pop();

					if (!job) {
						for (u32 j = 1; j < workerCount; ++j) {
							u32 target = (i + j) % workerCount;
							job = _data->queues[target]->Steal();
							if (job)
								break;
						}
					}

					if (job) {
						ExecuteNode(job);
					} else {
						_data->queues[i]->WaitAndPop(job, _data->isRunning);
						if (job) {
							ExecuteNode(job);
						}
					}
				}
			});
		}

		SLT_LOG_INFO("Core", "JobSystem initialized with {} worker(s)", workerCount);
	}

	void JobSystem::Terminate() {
		if (!_data)
			return;

		_data->isRunning.store(false, std::memory_order_release);
		for (auto& queue : _data->queues) {
			queue->NotifyAll();
		}

		for (auto& thread : _data->workerThreads) {
			if (thread.joinable()) {
				thread.join();
			}
		}

		{
			std::lock_guard lock(_data->longRunningMutex);
			for (auto& thread : _data->longRunningThreads) {
				if (thread.joinable()) {
					thread.join();
				}
			}
		}

		delete _data;
		_data = nullptr;
		SLT_LOG_INFO("Core", "JobSystem gracefully shut down");
	}

	JobHandle JobSystem::SubmitPayload(void (*func)(void*), JobConstructFunc construct, void* srcPayload, std::span<const JobHandle> dependencies, JobType type) {
		JobNode* node = AllocateNode();
		node->function = func;
		node->type = type;

		if (construct && srcPayload) {
			construct(node->payload, srcPayload);
		}

		node->unfinishedDependencies.store(static_cast<i32>(dependencies.size()) + 1, std::memory_order_relaxed);

		for (const JobHandle& dep : dependencies) {
			JobNode* depNode = Resolve(dep);
			bool added = false;

			if (depNode) {
				std::lock_guard lock(depNode->mutex);
				if (depNode->generation.load(std::memory_order_acquire) == dep.generation && !depNode->isCompleted) {
					JobEdge* edge = AllocateEdge();
					edge->waitingJob = node;
					edge->next = depNode->continuationList;
					depNode->continuationList = edge;
					added = true;
				}
			}

			if (!added) {
				node->unfinishedDependencies.fetch_sub(1, std::memory_order_relaxed);
			}
		}

		// before the final decrement (node may recycle)
		JobHandle handle = {node->index, node->generation.load(std::memory_order_relaxed)};

		if (node->unfinishedDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			DispatchNode(node);
		}

		return handle;
	}

	void JobSystem::WaitFor(JobHandle handle) {
		JobNode* node = Resolve(handle);
		if (!node)
			return;

		while (node->generation.load(std::memory_order_acquire) == handle.generation) {
			JobNode* job = nullptr;

			if (_workerIndex != 0xFFFFFFFF) {
				job = _data->queues[_workerIndex]->Pop();
				if (!job) {
					for (u32 j = 1; j < _data->queues.size(); ++j) {
						u32 target = (_workerIndex + j) % _data->queues.size();
						job = _data->queues[target]->Steal();
						if (job)
							break;
					}
				}
			} else {
				static thread_local u32 stealCounter = 0;
				u32 target = stealCounter++ % _data->queues.size();
				job = _data->queues[target]->Steal();
			}

			if (job) {
				ExecuteNode(job);
			} else {
				std::this_thread::yield();
			}
		}
	}

	void JobSystem::WaitForAll(std::span<const JobHandle> handles) {
		for (const JobHandle& handle : handles) {
			WaitFor(handle);
		}
	}

	u32 JobSystem::GetWorkerCount() {
		return _data ? static_cast<u32>(_data->workerThreads.size()) : 0;
	}

} // namespace Shalltear

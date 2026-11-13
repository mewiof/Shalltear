#include "NetworkInternal.h"

#include <algorithm>

namespace Shalltear::Net {

	namespace {

		struct DispatchState {
			std::mutex lock;
			Vector<std::function<void()>> posted{GetMemoryResource(MemTag::Platform)};
			Vector<std::weak_ptr<WatchedCore>> watched{GetMemoryResource(MemTag::Platform)};
			Vector<std::shared_ptr<WatchedCore>> lingering{GetMemoryResource(MemTag::Platform)};
		};

		DispatchState& State() {
			static DispatchState state;
			return state;
		}

	} // anonymous namespace

	void Dispatcher::Post(std::function<void()> job) {
		DispatchState& state = State();
		std::lock_guard<std::mutex> guard(state.lock);
		state.posted.push_back(std::move(job));
	}

	void Dispatcher::Watch(const std::shared_ptr<WatchedCore>& core) {
		DispatchState& state = State();
		std::lock_guard<std::mutex> guard(state.lock);
		state.watched.push_back(core);
	}

	void Dispatcher::Linger(std::shared_ptr<WatchedCore> core) {
		DispatchState& state = State();
		std::lock_guard<std::mutex> guard(state.lock);
		state.lingering.push_back(std::move(core));
	}

	void Dispatch() {
		DispatchState& state = State();
		Vector<std::shared_ptr<WatchedCore>> lingering{GetMemoryResource(MemTag::Platform)};
		Vector<std::function<void()>> jobs{GetMemoryResource(MemTag::Platform)};
		Vector<std::shared_ptr<WatchedCore>> live{GetMemoryResource(MemTag::Platform)};
		{
			std::lock_guard<std::mutex> guard(state.lock);
			jobs.swap(state.posted);
			std::erase_if(state.watched, [&](const std::weak_ptr<WatchedCore>& entry) {
				std::shared_ptr<WatchedCore> core = entry.lock();
				if (!core) {
					return true;
				}
				live.push_back(std::move(core));
				return false;
			});
			lingering = state.lingering;
			live.insert(live.end(), lingering.begin(), lingering.end());
		}
		for (std::function<void()>& job : jobs) {
			job();
		}

		Vector<SocketBackend::PollItem> items{GetMemoryResource(MemTag::Platform)};
		Vector<u32> itemOf{GetMemoryResource(MemTag::Platform)};
		itemOf.resize(live.size(), ~0u);
		for (u32 i = 0; i < live.size(); ++i) {
			const SocketBackend::Handle handle = live[i]->PollHandle();
			if (handle != SocketBackend::INVALID) {
				itemOf[i] = static_cast<u32>(items.size());
				items.push_back({handle});
			}
		}
		SocketBackend::PollReadable(items);
		for (u32 i = 0; i < live.size(); ++i) {
			const SocketBackend::PollItem* item = itemOf[i] != ~0u ? &items[itemOf[i]] : nullptr;
			live[i]->Pump(item && item->readable, item && item->failed);
		}
		std::erase_if(lingering, [](const std::shared_ptr<WatchedCore>& core) { return core->Lingering(); });
		if (!lingering.empty()) {
			std::lock_guard<std::mutex> guard(state.lock);
			std::erase_if(state.lingering, [&](const std::shared_ptr<WatchedCore>& core) {
				return std::find(lingering.begin(), lingering.end(), core) != lingering.end();
			});
		}
	}

} // namespace Shalltear::Net

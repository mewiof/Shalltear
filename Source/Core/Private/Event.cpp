#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"

#include "Module/EngineStateTable.h"

namespace Shalltear {

	namespace {
		EventBus* _boundBus = nullptr;
	} // anonymous namespace

	EventBus& EventBus::GetGlobal() {
		if (_boundBus)
			return *_boundBus;
		static EventBus instance;
		return instance;
	}

	void EventBus::AddDelegate(StringHash eventId, SharedPtr<IEventDelegate> delegate) {
		std::lock_guard<std::mutex> lock(_mutex);
		_events[eventId].push_back(std::move(delegate));
	}

	void EventBus::Dispatch(StringHash eventId, const void* payload) {
		std::vector<SharedPtr<IEventDelegate>> delegatesToInvoke;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _events.find(eventId);
			if (it != _events.end()) {
				for (const auto& delegate : it->second) {
					if (!delegate->IsCancelled()) {
						delegatesToInvoke.push_back(delegate);
					}
				}
			}
		}

		bool needsCleanup = false;
		for (const auto& delegate : delegatesToInvoke) {
			if (!delegate->IsCancelled()) {
				if (!delegate->Invoke(payload)) {
					needsCleanup = true;
				}
			}
		}

		if (needsCleanup) {
			std::lock_guard<std::mutex> lock(_mutex);
			auto it = _events.find(eventId);
			if (it != _events.end()) {
				auto& delegates = it->second;
				auto eraseIt = std::remove_if(delegates.begin(), delegates.end(), [](const SharedPtr<IEventDelegate>& d) {
					return d->IsCancelled() || ObjectRegistry::Resolve(d->GetObjectId()) == nullptr;
				});
				delegates.erase(eraseIt, delegates.end());
			}
		}
	}

	void EventBus::Unsubscribe(StringHash eventId, u64 objectId) {
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _events.find(eventId);
		if (it != _events.end()) {
			auto& delegates = it->second;
			for (auto& delegate : delegates) {
				if (delegate->GetObjectId() == objectId) {
					delegate->Cancel();
				}
			}
			auto eraseIt = std::remove_if(delegates.begin(), delegates.end(), [](const SharedPtr<IEventDelegate>& d) {
				return d->IsCancelled();
			});
			delegates.erase(eraseIt, delegates.end());
		}
	}

	void EventBus::UnsubscribeAll(u64 objectId) {
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto& [eventId, delegates] : _events) {
			for (auto& delegate : delegates) {
				if (delegate->GetObjectId() == objectId) {
					delegate->Cancel();
				}
			}
			auto eraseIt = std::remove_if(delegates.begin(), delegates.end(), [](const SharedPtr<IEventDelegate>& d) {
				return d->IsCancelled();
			});
			delegates.erase(eraseIt, delegates.end());
		}
	}

	namespace ModuleInternal {

		void CaptureEventBusEngineState(EngineStateTable& table) {
			table.eventBus = &EventBus::GetGlobal();
		}

		void BindEventBusEngineState(const EngineStateTable& table) {
			_boundBus = static_cast<EventBus*>(table.eventBus);
		}

	} // namespace ModuleInternal

} // namespace Shalltear

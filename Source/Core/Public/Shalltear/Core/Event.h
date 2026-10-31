#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Shalltear {

	class IEventDelegate {
	public:
		virtual ~IEventDelegate() = default;

		virtual bool Invoke(const void* payload) = 0;
		virtual u64 GetObjectId() const = 0;

		void Cancel() {
			_isCancelled.store(true, std::memory_order_relaxed);
		}
		bool IsCancelled() const {
			return _isCancelled.load(std::memory_order_relaxed);
		}
	protected:
		std::atomic<bool> _isCancelled{false};
	};

	template <typename TSubscriber, typename TPayload, typename TArg>
	class MemberEventDelegate : public IEventDelegate {
	public:
		using CallbackType = void (TSubscriber::*)(TArg);

		MemberEventDelegate(Handle<TSubscriber> subscriber, CallbackType callback)
			: _subscriber(subscriber), _callback(callback) {}

		bool Invoke(const void* payload) override {
			TSubscriber* instance = _subscriber.Get();
			if (!instance) {
				return false;
			}

			const TPayload* typedPayload = static_cast<const TPayload*>(payload);
			(instance->*_callback)(*typedPayload);
			return true;
		}

		u64 GetObjectId() const override {
			return _subscriber.GetInstanceId();
		}
	private:
		Handle<TSubscriber> _subscriber;
		CallbackType _callback;
	};

	class SHALLTEAR_CORE_API EventBus {
	public:
		EventBus() = default;
		~EventBus() = default;
		EventBus(const EventBus&) = delete;
		EventBus& operator=(const EventBus&) = delete;

		static EventBus& GetGlobal();

		template <typename TPayload, typename TSubscriber, typename TArg>
		void Subscribe(StringHash eventId, Handle<TSubscriber> subscriber, void (TSubscriber::*callback)(TArg)) {
			SharedPtr<IEventDelegate> delegate = MakeShared<MemberEventDelegate<TSubscriber, TPayload, TArg>>(subscriber, callback);
			AddDelegate(eventId, delegate);
		}

		template <typename TPayload>
		void Publish(StringHash eventId, const TPayload& payload) {
			Dispatch(eventId, &payload);
		}

		void Unsubscribe(StringHash eventId, u64 objectId);
		void UnsubscribeAll(u64 objectId);
	private:
		void AddDelegate(StringHash eventId, SharedPtr<IEventDelegate> delegate);
		void Dispatch(StringHash eventId, const void* payload);

		std::unordered_map<StringHash, std::vector<SharedPtr<IEventDelegate>>> _events;
		std::mutex _mutex;
	};

	struct EmptyEventPayload {};

	template <typename TPayload = EmptyEventPayload>
	class Event {
	public:
		explicit constexpr Event(StringHash id)
			: _id(id) {}

		constexpr StringHash GetId() const {
			return _id;
		}

		template <typename TSubscriber, typename TArg>
		void Subscribe(EventBus& bus, Handle<TSubscriber> subscriber, void (TSubscriber::*callback)(TArg)) const {
			static_assert(std::is_constructible_v<TArg, const TPayload&>, "Callback argument must be constructible from event payload");
			bus.Subscribe<TPayload, TSubscriber, TArg>(_id, subscriber, callback);
		}

		void Publish(EventBus& bus, const TPayload& payload) const {
			bus.Publish<TPayload>(_id, payload);
		}

		void Unsubscribe(EventBus& bus, u64 objectId) const {
			bus.Unsubscribe(_id, objectId);
		}
	private:
		StringHash _id;
	};

} // namespace Shalltear

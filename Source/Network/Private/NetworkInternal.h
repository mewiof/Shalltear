#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/Address.h"
#include "Shalltear/Network/Socket.h"

#include "SocketBackend.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace Shalltear::Net {

	struct WatchedCore {
		virtual ~WatchedCore() = default;
		virtual SocketBackend::Handle PollHandle() = 0;
		// callbacks fired after lock is released
		virtual void Pump(bool readable, bool failed) = 0;
		virtual bool Lingering() { return false; }
	};

	enum class SocketKind : u8 {
		TCP,
		Listener,
		UDP,
	};

	struct SocketCore : WatchedCore, std::enable_shared_from_this<SocketCore> {
		explicit SocketCore(SocketKind socketKind) : kind(socketKind) {}
		~SocketCore() override;

		SocketBackend::Handle PollHandle() override;
		void Pump(bool readable, bool failed) override;

		void AdvanceLocked();
		void FailLocked(str message);
		void TryNextCandidateLocked();
		void CloseLocked();
		void CloseAttemptsLocked();

		const SocketKind kind;
		mutable std::mutex lock;
		SocketBackend::Handle handle = SocketBackend::INVALID;
		SocketState state = SocketState::Connecting;
		str error;
		Address peer;
		u16 localPort = 0;

		struct Attempt {
			SocketBackend::Handle handle = SocketBackend::INVALID;
			Address target;
			std::chrono::steady_clock::time_point started{};
		};
		bool resolving = false;
		Vector<Address> candidates{GetMemoryResource(MemTag::Platform)};
		u32 nextCandidate = 0;
		Vector<Attempt> attempts{GetMemoryResource(MemTag::Platform)};

		SocketCallback callback;
		bool watched = false;
		bool openedReported = false;
		bool endReported = false;
	};

	std::shared_ptr<SocketCore> MakeConnectingCore(std::string_view host, u16 port);
	std::shared_ptr<SocketCore> MakeOpenCore(SocketBackend::Handle handle, const Address& peer);

	Address MapToV6(const Address& address);
	Address UnmapV4(const Address& address);

	namespace Dispatcher {
		void Post(std::function<void()> job);
		void Watch(const std::shared_ptr<WatchedCore>& core);
		// held/pumped until Lingering is false
		void Linger(std::shared_ptr<WatchedCore> core);
	} // namespace Dispatcher

	namespace Workers {
		enum class Pool : u8 {
			Resolve,
			HTTP,
		};
		void Run(Pool pool, std::function<void()> job);
		// workers joined, queued jobs dropped
		void Shutdown();
	} // namespace Workers

	namespace HTTPInternal {
		void CancelAll();
		void DropCallbacks();
		void Touch();
		void StopWatchdog();
	} // namespace HTTPInternal

} // namespace Shalltear::Net

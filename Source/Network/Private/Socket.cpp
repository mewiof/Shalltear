#include "Shalltear/Network/Socket.h"

#include "NetworkInternal.h"
#include "SocketBackend.h"

#include <format>

namespace Shalltear::Net {

	namespace {

		constexpr std::chrono::seconds ConnectAttemptLimit{10};
		// RFC 8305's connection attempt delay
		constexpr std::chrono::milliseconds NextAttemptDelay{250};

		void Fire(const SocketCallback& callback, std::span<const SocketEvent> events) {
			if (!callback) {
				return;
			}
			for (SocketEvent event : events) {
				callback(event);
			}
		}

		void Observe(const std::shared_ptr<SocketCore>& core, SocketCallback callback) {
			bool first = false;
			{
				std::lock_guard<std::mutex> guard(core->lock);
				core->callback = std::move(callback);
				first = core->callback && !core->watched;
				core->watched = core->watched || first;
			}
			if (first) {
				Dispatcher::Watch(core);
			}
		}

	} // anonymous namespace

	SocketCore::~SocketCore() {
		CloseAttemptsLocked();
		SocketBackend::Close(handle);
	}

	void SocketCore::CloseAttemptsLocked() {
		for (const Attempt& attempt : attempts) {
			SocketBackend::Close(attempt.handle);
		}
		attempts.clear();
	}

	SocketBackend::Handle SocketCore::PollHandle() {
		std::lock_guard<std::mutex> guard(lock);
		return state == SocketState::Open ? handle : SocketBackend::INVALID;
	}

	void SocketCore::Pump(bool readable, bool failed) {
		SocketEvent events[3];
		u32 count = 0;
		SocketCallback fire;
		{
			std::lock_guard<std::mutex> guard(lock);
			AdvanceLocked();
			if (failed && state == SocketState::Open && kind != SocketKind::TCP) {
				FailLocked("socket failed");
			}
			if (state == SocketState::Open && kind == SocketKind::TCP && !openedReported) {
				openedReported = true;
				events[count++] = SocketEvent::Opened;
			}
			if (state == SocketState::Open && readable) {
				events[count++] = SocketEvent::Readable;
			}
			if ((state == SocketState::Closed || state == SocketState::Failed) && !endReported) {
				endReported = true;
				events[count++] = state == SocketState::Closed ? SocketEvent::Closed : SocketEvent::Failed;
			}
			fire = callback;
		}
		Fire(fire, {events, count});
	}

	void SocketCore::FailLocked(str message) {
		CloseAttemptsLocked();
		SocketBackend::Close(handle);
		handle = SocketBackend::INVALID;
		error = std::move(message);
		state = SocketState::Failed;
	}

	void SocketCore::CloseLocked() {
		CloseAttemptsLocked();
		SocketBackend::Close(handle);
		handle = SocketBackend::INVALID;
		if (state != SocketState::Failed) {
			state = SocketState::Closed;
		}
	}

	void SocketCore::TryNextCandidateLocked() {
		while (nextCandidate < candidates.size()) {
			const Address target = candidates[nextCandidate++];
			str attemptError;
			const SocketBackend::Handle opened = SocketBackend::Open(true, target.v6, attemptError);
			if (opened == SocketBackend::INVALID) {
				error = attemptError;
				continue;
			}
			const SocketBackend::Result result = SocketBackend::Connect(opened, target, attemptError);
			if (result == SocketBackend::Result::Done) {
				CloseAttemptsLocked();
				handle = opened;
				peer = target;
				error.clear();
				state = SocketState::Open;
				return;
			}
			if (result == SocketBackend::Result::WouldBlock) {
				attempts.push_back({opened, target, std::chrono::steady_clock::now()});
				return;
			}
			SocketBackend::Close(opened);
			error = attemptError;
		}
		if (attempts.empty()) {
			FailLocked(error.empty() ? str("no address to connect to") : error);
		}
	}

	void SocketCore::AdvanceLocked() {
		if (state != SocketState::Connecting || resolving) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		for (size_t i = 0; i < attempts.size();) {
			str attemptError;
			const SocketBackend::Result result = SocketBackend::ConnectResult(attempts[i].handle, attemptError);
			if (result == SocketBackend::Result::Done) {
				const Attempt won = attempts[i];
				attempts.erase(attempts.begin() + static_cast<ptrdiff_t>(i));
				CloseAttemptsLocked();
				handle = won.handle;
				peer = won.target;
				error.clear();
				state = SocketState::Open;
				return;
			}
			if (result == SocketBackend::Result::WouldBlock && now - attempts[i].started <= ConnectAttemptLimit) {
				++i;
				continue;
			}
			SocketBackend::Close(attempts[i].handle);
			error = result == SocketBackend::Result::WouldBlock ? str("connect timed out") : attemptError;
			attempts.erase(attempts.begin() + static_cast<ptrdiff_t>(i));
		}
		if (attempts.empty() || (nextCandidate < candidates.size() && now - attempts.back().started > NextAttemptDelay)) {
			TryNextCandidateLocked();
		}
	}

	std::shared_ptr<SocketCore> MakeConnectingCore(std::string_view host, u16 port) {
		auto core = std::make_shared<SocketCore>(SocketKind::TCP);
		Address literal;
		if (Address::Parse(host, literal)) {
			literal.port = port;
			std::lock_guard<std::mutex> guard(core->lock);
			core->candidates.push_back(literal);
			core->TryNextCandidateLocked();
			return core;
		}
		core->resolving = true;
		Workers::Run(Workers::Pool::Resolve, [weak = std::weak_ptr<SocketCore>(core), name = str(host), port] {
			Vector<Address> found{GetMemoryResource(MemTag::Platform)};
			str failure;
			const bool resolved = SocketBackend::Resolve(name, port, found, failure);
			std::shared_ptr<SocketCore> target = weak.lock();
			if (!target) {
				return;
			}
			std::lock_guard<std::mutex> guard(target->lock);
			target->resolving = false;
			if (target->state != SocketState::Connecting) {
				return;
			}
			if (!resolved) {
				target->FailLocked(std::format("can't resolve '{}': {}", name, failure));
				return;
			}
			target->candidates = std::move(found);
			target->TryNextCandidateLocked();
		});
		return core;
	}

	std::shared_ptr<SocketCore> MakeOpenCore(SocketBackend::Handle handle, const Address& peer) {
		auto core = std::make_shared<SocketCore>(SocketKind::TCP);
		core->handle = handle;
		core->peer = UnmapV4(peer);
		core->state = SocketState::Open;
		return core;
	}

	TCPSocket::TCPSocket(std::shared_ptr<SocketCore> core) : _core(std::move(core)) {}

	TCPSocket::~TCPSocket() {
		if (!_core) {
			return;
		}
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->callback = nullptr;
		_core->endReported = true;
		_core->CloseLocked();
	}

	std::shared_ptr<SocketCore> TCPSocket::Detach() {
		if (_core) {
			std::lock_guard<std::mutex> guard(_core->lock);
			_core->callback = nullptr;
		}
		return std::move(_core);
	}

	std::unique_ptr<TCPSocket> TCPSocket::Connect(std::string_view host, u16 port) {
		return std::make_unique<TCPSocket>(MakeConnectingCore(host, port));
	}

	std::unique_ptr<TCPSocket> TCPSocket::Connect(const Address& address) {
		auto core = std::make_shared<SocketCore>(SocketKind::TCP);
		std::lock_guard<std::mutex> guard(core->lock);
		core->candidates.push_back(address);
		core->TryNextCandidateLocked();
		return std::make_unique<TCPSocket>(core);
	}

	SocketState TCPSocket::GetState() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->AdvanceLocked();
		return _core->state;
	}

	size_t TCPSocket::Send(std::span<const u8> data) {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->AdvanceLocked();
		if (_core->state != SocketState::Open) {
			return 0;
		}
		size_t sent = 0;
		str failure;
		const SocketBackend::Result result = SocketBackend::Send(_core->handle, data, sent, failure);
		if (result == SocketBackend::Result::Closed) {
			_core->error = failure;
			_core->CloseLocked();
		} else if (result == SocketBackend::Result::Error) {
			_core->FailLocked(failure);
		}
		return sent;
	}

	size_t TCPSocket::Receive(std::span<u8> out) {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->AdvanceLocked();
		if (_core->state != SocketState::Open) {
			return 0;
		}
		size_t received = 0;
		str failure;
		const SocketBackend::Result result = SocketBackend::Receive(_core->handle, out, received, failure);
		if (result == SocketBackend::Result::Closed) {
			_core->error = failure;
			_core->CloseLocked();
		} else if (result == SocketBackend::Result::Error) {
			_core->FailLocked(failure);
		}
		return received;
	}

	void TCPSocket::Close() {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->endReported = true;
		_core->CloseLocked();
	}

	str TCPSocket::GetError() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		return _core->error;
	}

	Address TCPSocket::GetPeerAddress() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		return UnmapV4(_core->peer);
	}

	void TCPSocket::SetNoDelay(bool noDelay) {
		std::lock_guard<std::mutex> guard(_core->lock);
		if (_core->handle != SocketBackend::INVALID) {
			SocketBackend::SetNoDelay(_core->handle, noDelay);
		}
	}

	void TCPSocket::SetCallback(SocketCallback callback) {
		Observe(_core, std::move(callback));
	}

	TCPListener::TCPListener(std::shared_ptr<SocketCore> core) : _core(std::move(core)) {}

	TCPListener::~TCPListener() {
		Close();
	}

	std::unique_ptr<TCPListener> TCPListener::Listen(const Address& at, str* outError, u32 backlog) {
		str failure;
		const SocketBackend::Handle handle = SocketBackend::Open(true, at.v6, failure);
		Address bound;
		if (handle == SocketBackend::INVALID || !SocketBackend::Bind(handle, at, failure) || !SocketBackend::Listen(handle, backlog, failure) ||
			!SocketBackend::LocalAddress(handle, bound)) {
			SocketBackend::Close(handle);
			if (outError) {
				*outError = failure;
			}
			return nullptr;
		}
		auto core = std::make_shared<SocketCore>(SocketKind::Listener);
		core->handle = handle;
		core->localPort = bound.port;
		core->state = SocketState::Open;
		return std::make_unique<TCPListener>(std::move(core));
	}

	u16 TCPListener::GetPort() const {
		return _core->localPort;
	}

	std::unique_ptr<TCPSocket> TCPListener::Accept() {
		std::lock_guard<std::mutex> guard(_core->lock);
		if (_core->state != SocketState::Open) {
			return nullptr;
		}
		SocketBackend::Handle accepted = SocketBackend::INVALID;
		Address peer;
		str failure;
		if (SocketBackend::Accept(_core->handle, accepted, peer, failure) != SocketBackend::Result::Done) {
			return nullptr;
		}
		return std::make_unique<TCPSocket>(MakeOpenCore(accepted, peer));
	}

	void TCPListener::Close() {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->callback = nullptr;
		_core->endReported = true;
		_core->CloseLocked();
	}

	void TCPListener::SetCallback(SocketCallback callback) {
		Observe(_core, std::move(callback));
	}

	UDPSocket::UDPSocket(std::shared_ptr<SocketCore> core) : _core(std::move(core)) {}

	UDPSocket::~UDPSocket() {
		Close();
	}

	std::unique_ptr<UDPSocket> UDPSocket::Bind(const Address& at, str* outError) {
		str failure;
		const SocketBackend::Handle handle = SocketBackend::Open(false, at.v6, failure);
		Address bound;
		if (handle == SocketBackend::INVALID || !SocketBackend::Bind(handle, at, failure) || !SocketBackend::LocalAddress(handle, bound)) {
			SocketBackend::Close(handle);
			if (outError) {
				*outError = failure;
			}
			return nullptr;
		}
		auto core = std::make_shared<SocketCore>(SocketKind::UDP);
		core->handle = handle;
		core->localPort = bound.port;
		core->peer = bound;
		core->state = SocketState::Open;
		return std::make_unique<UDPSocket>(std::move(core));
	}

	u16 UDPSocket::GetPort() const {
		return _core->localPort;
	}

	bool UDPSocket::SendTo(const Address& to, std::span<const u8> datagram) {
		std::lock_guard<std::mutex> guard(_core->lock);
		if (_core->state != SocketState::Open) {
			return false;
		}
		const Address target = _core->peer.v6 ? MapToV6(to) : to;
		str failure;
		const SocketBackend::Result result = SocketBackend::SendTo(_core->handle, target, datagram, failure);
		if (result == SocketBackend::Result::Error) {
			_core->error = failure;
		}
		return result == SocketBackend::Result::Done;
	}

	size_t UDPSocket::ReceiveFrom(std::span<u8> out, Address& from) {
		std::lock_guard<std::mutex> guard(_core->lock);
		if (_core->state != SocketState::Open) {
			return 0;
		}
		size_t received = 0;
		Address sender;
		str failure;
		const SocketBackend::Result result = SocketBackend::ReceiveFrom(_core->handle, out, received, sender, failure);
		if (result != SocketBackend::Result::Done) {
			if (result == SocketBackend::Result::Error) {
				_core->error = failure;
			}
			return 0;
		}
		from = UnmapV4(sender);
		return received;
	}

	void UDPSocket::Close() {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->callback = nullptr;
		_core->endReported = true;
		_core->CloseLocked();
	}

	str UDPSocket::GetError() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		return _core->error;
	}

	void UDPSocket::SetCallback(SocketCallback callback) {
		Observe(_core, std::move(callback));
	}

} // namespace Shalltear::Net

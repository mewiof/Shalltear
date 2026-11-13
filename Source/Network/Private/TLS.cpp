#include "Shalltear/Network/TLS.h"

#include "NetworkInternal.h"
#include "TLSBackend.h"

#include <algorithm>
#include <chrono>

namespace Shalltear::Net {

	namespace {

		constexpr size_t SEND_LIMIT = 1 << 20;
		constexpr size_t READ_CHUNK = 16 * 1024;
		constexpr size_t RECEIVE_LIMIT = 4 << 20;
		constexpr std::chrono::seconds LingerLimit{5};

		int HexValue(char c) {
			if (c >= '0' && c <= '9') {
				return c - '0';
			}
			if (c >= 'a' && c <= 'f') {
				return c - 'a' + 10;
			}
			if (c >= 'A' && c <= 'F') {
				return c - 'A' + 10;
			}
			return -1;
		}

	} // anonymous namespace

	bool ParseCertificateHash(std::string_view hex, CertificateHash& out) {
		CertificateHash parsed{};
		size_t nibbles = 0;
		for (char c : hex) {
			if (c == ':' || c == ' ') {
				continue;
			}
			const int value = HexValue(c);
			if (value < 0 || nibbles >= parsed.size() * 2) {
				return false;
			}
			parsed[nibbles / 2] = static_cast<u8>(parsed[nibbles / 2] << 4 | value);
			++nibbles;
		}
		if (nibbles != parsed.size() * 2) {
			return false;
		}
		out = parsed;
		return true;
	}

	TLSIdentity::TLSIdentity(std::unique_ptr<TLSIdentityData> data) : _data(std::move(data)) {}

	TLSIdentity::~TLSIdentity() = default;

	std::shared_ptr<TLSIdentity> TLSIdentity::FromPKCS12(std::span<const u8> pfx, std::string_view password, str* outError) {
		auto data = std::make_unique<TLSIdentityData>();
		str error;
		if (!TLSBackend::LoadPKCS12(pfx, password, *data, error)) {
			if (outError) {
				*outError = error;
			}
			return nullptr;
		}
		return std::make_shared<TLSIdentity>(std::move(data));
	}

	const CertificateHash& TLSIdentity::GetCertificateHash() const {
		return _data->hash;
	}

	struct TLSCore : WatchedCore, std::enable_shared_from_this<TLSCore> {
		~TLSCore() override { TLSBackend::Destroy(session); }

		SocketBackend::Handle PollHandle() override {
			std::lock_guard<std::mutex> guard(lock);
			std::lock_guard<std::mutex> tcpGuard(tcp->lock);
			return tcp->state == SocketState::Open ? tcp->handle : SocketBackend::INVALID;
		}

		bool Lingering() override {
			std::lock_guard<std::mutex> guard(lock);
			return closing;
		}

		void FinishClosingLocked() {
			Flush();
			std::lock_guard<std::mutex> tcpGuard(tcp->lock);
			if (rawOut.empty() || tcp->state != SocketState::Open || std::chrono::steady_clock::now() > lingerUntil) {
				tcp->CloseLocked();
				closing = false;
			}
		}

		void Pump(bool, bool) override {
			SocketEvent events[3];
			u32 count = 0;
			SocketCallback fire;
			{
				std::lock_guard<std::mutex> guard(lock);
				if (closing) {
					FinishClosingLocked();
					return;
				}
				PumpLocked();
				fire = callback;
				if (!fire) {
					return;
				}
				if (state == SocketState::Open && !openedReported) {
					openedReported = true;
					events[count++] = SocketEvent::Opened;
				}
				if (state == SocketState::Open && !plainIn.empty()) {
					events[count++] = SocketEvent::Readable;
				}
				if ((state == SocketState::Closed || state == SocketState::Failed) && !endReported) {
					endReported = true;
					events[count++] = state == SocketState::Closed ? SocketEvent::Closed : SocketEvent::Failed;
				}
			}
			for (u32 i = 0; i < count; ++i) {
				fire(events[i]);
			}
		}

		void Fail(str message) {
			if (state == SocketState::Closed || state == SocketState::Failed) {
				return;
			}
			error = std::move(message);
			state = SocketState::Failed;
			std::lock_guard<std::mutex> tcpGuard(tcp->lock);
			tcp->CloseLocked();
		}

		void Flush() {
			if (rawOut.empty()) {
				return;
			}
			std::lock_guard<std::mutex> tcpGuard(tcp->lock);
			while (!rawOut.empty() && tcp->state == SocketState::Open) {
				size_t sent = 0;
				str failure;
				const SocketBackend::Result result = SocketBackend::Send(tcp->handle, rawOut, sent, failure);
				rawOut.erase(rawOut.begin(), rawOut.begin() + static_cast<ptrdiff_t>(sent));
				if (result == SocketBackend::Result::WouldBlock || (result == SocketBackend::Result::Done && sent == 0)) {
					return;
				}
				if (result != SocketBackend::Result::Done) {
					tcp->error = failure;
					tcp->CloseLocked();
				}
			}
		}

		bool ReadRaw() {
			std::lock_guard<std::mutex> tcpGuard(tcp->lock);
			bool any = false;
			u8 chunk[READ_CHUNK];
			while (tcp->state == SocketState::Open) {
				size_t received = 0;
				str failure;
				const SocketBackend::Result result = SocketBackend::Receive(tcp->handle, chunk, received, failure);
				if (result == SocketBackend::Result::Done && received > 0) {
					rawIn.insert(rawIn.end(), chunk, chunk + received);
					any = true;
					continue;
				}
				if (result == SocketBackend::Result::Closed || result == SocketBackend::Result::Error) {
					tcp->error = failure;
					tcp->CloseLocked();
				}
				break;
			}
			return any;
		}

		void PumpLocked() {
			if (state == SocketState::Closed || state == SocketState::Failed) {
				return;
			}
			SocketState tcpState = SocketState::Connecting;
			{
				std::lock_guard<std::mutex> tcpGuard(tcp->lock);
				tcp->AdvanceLocked();
				tcpState = tcp->state;
				if (tcpState == SocketState::Failed) {
					error = tcp->error;
				}
			}
			if (tcpState == SocketState::Connecting) {
				return;
			}
			if (tcpState == SocketState::Failed && !session) {
				state = SocketState::Failed;
				return;
			}
			if (!session && tcpState == SocketState::Open) {
				str failure;
				session = server ? TLSBackend::CreateServer(identity->GetData(), failure) : TLSBackend::CreateClient(serverName, failure);
				if (!session) {
					Fail(failure);
					return;
				}
			}
			if (!session) {
				state = SocketState::Closed;
				return;
			}
			Flush();
			if (plainIn.size() < RECEIVE_LIMIT) {
				ReadRaw();
			}
			str failure;
			const TLSBackend::Step step = TLSBackend::Advance(session, rawIn, rawOut, plainIn, failure);
			Flush();
			if (step == TLSBackend::Step::Failed) {
				Fail(failure);
				return;
			}
			if (step == TLSBackend::Step::Open && state == SocketState::Connecting) {
				if (!Validate()) {
					return;
				}
				state = SocketState::Open;
			}
			bool tcpEnded = false;
			{
				std::lock_guard<std::mutex> tcpGuard(tcp->lock);
				tcpEnded = tcp->state != SocketState::Open;
			}
			if (step == TLSBackend::Step::Closed || tcpEnded) {
				if (state == SocketState::Connecting) {
					Fail(error.empty() ? str("connection closed during TLS handshake") : error);
					return;
				}
				if (plainIn.empty()) {
					state = SocketState::Closed;
					std::lock_guard<std::mutex> tcpGuard(tcp->lock);
					tcp->CloseLocked();
				}
			}
		}

		bool Validate() {
			if (server) {
				return true;
			}
			if (pinned) {
				CertificateHash presented{};
				if (!TLSBackend::PeerCertificateHash(session, presented)) {
					Fail("server sent no certificate");
					return false;
				}
				if (presented != *pinned) {
					Fail("certificate doesn't match pinned hash");
					return false;
				}
				return true;
			}
			str failure;
			if (!TLSBackend::ValidatePeer(session, serverName, failure)) {
				Fail(failure);
				return false;
			}
			return true;
		}

		std::mutex lock;
		std::shared_ptr<SocketCore> tcp;
		TLSBackend::Session* session = nullptr;
		bool server = false;
		str serverName;
		std::optional<CertificateHash> pinned;
		std::shared_ptr<TLSIdentity> identity;

		SocketState state = SocketState::Connecting;
		str error;
		Vector<u8> rawIn{GetMemoryResource(MemTag::Platform)};
		Vector<u8> rawOut{GetMemoryResource(MemTag::Platform)};
		Vector<u8> plainIn{GetMemoryResource(MemTag::Platform)};

		SocketCallback callback;
		bool openedReported = false;
		bool endReported = false;
		bool closing = false;
		std::chrono::steady_clock::time_point lingerUntil{};
	};

	TLSStream::TLSStream(std::shared_ptr<TLSCore> core) : _core(std::move(core)) {
		Dispatcher::Watch(_core);
	}

	TLSStream::~TLSStream() {
		Close();
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->callback = nullptr;
	}

	std::unique_ptr<TLSStream> TLSStream::Connect(std::string_view host, u16 port, TLSOptions options) {
		auto core = std::make_shared<TLSCore>();
		core->tcp = MakeConnectingCore(host, port);
		core->serverName = options.serverName.empty() ? str(host) : std::move(options.serverName);
		core->pinned = options.pinnedCertificate;
		return std::make_unique<TLSStream>(std::move(core));
	}

	std::unique_ptr<TLSStream> TLSStream::Accept(std::unique_ptr<TCPSocket> socket, std::shared_ptr<TLSIdentity> identity) {
		auto core = std::make_shared<TLSCore>();
		core->server = true;
		core->identity = std::move(identity);
		core->tcp = socket ? socket->Detach() : nullptr;
		if (!core->tcp || !core->identity) {
			core->tcp = core->tcp ? core->tcp : std::make_shared<SocketCore>(SocketKind::TCP);
			core->state = SocketState::Failed;
			core->error = core->identity ? "no connection to secure" : "no TLS identity";
		}
		return std::make_unique<TLSStream>(std::move(core));
	}

	bool TLSStream::IsAvailable() {
		return TLSBackend::IsAvailable();
	}

	SocketState TLSStream::GetState() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->PumpLocked();
		return _core->state;
	}

	size_t TLSStream::Send(std::span<const u8> data) {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->PumpLocked();
		if (_core->state != SocketState::Open || _core->rawOut.size() > SEND_LIMIT) {
			return 0;
		}
		str failure;
		if (!TLSBackend::Encrypt(_core->session, data, _core->rawOut, failure)) {
			_core->Fail(failure);
			return 0;
		}
		_core->Flush();
		return data.size();
	}

	size_t TLSStream::Receive(std::span<u8> out) {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->PumpLocked();
		const size_t count = std::min(out.size(), _core->plainIn.size());
		std::copy_n(_core->plainIn.begin(), count, out.begin());
		_core->plainIn.erase(_core->plainIn.begin(), _core->plainIn.begin() + static_cast<ptrdiff_t>(count));
		if (count > 0 && _core->plainIn.empty()) {
			_core->PumpLocked();
		}
		return count;
	}

	void TLSStream::Close() {
		bool linger = false;
		{
			std::lock_guard<std::mutex> guard(_core->lock);
			if (_core->closing) {
				return;
			}
			const bool wasOpen = _core->state == SocketState::Open && _core->session;
			if (wasOpen) {
				TLSBackend::Shutdown(_core->session, _core->rawOut);
				_core->Flush();
			}
			_core->endReported = true;
			if (_core->state != SocketState::Failed) {
				_core->state = SocketState::Closed;
			}
			linger = wasOpen && !_core->rawOut.empty();
			if (linger) {
				_core->closing = true;
				_core->lingerUntil = std::chrono::steady_clock::now() + LingerLimit;
			} else {
				std::lock_guard<std::mutex> tcpGuard(_core->tcp->lock);
				_core->tcp->CloseLocked();
			}
		}
		if (linger) {
			Dispatcher::Linger(_core);
		}
	}

	str TLSStream::GetError() const {
		std::lock_guard<std::mutex> guard(_core->lock);
		return _core->error;
	}

	bool TLSStream::GetPeerCertificateHash(CertificateHash& out) const {
		std::lock_guard<std::mutex> guard(_core->lock);
		return _core->session && TLSBackend::PeerCertificateHash(_core->session, out);
	}

	void TLSStream::SetCallback(SocketCallback callback) {
		std::lock_guard<std::mutex> guard(_core->lock);
		_core->callback = std::move(callback);
	}

} // namespace Shalltear::Net

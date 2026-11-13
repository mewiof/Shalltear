#pragma once

#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/Address.h"
#include "Shalltear/Network/NetworkAPI.h"

#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace Shalltear::Net {

	enum class SocketState : u8 {
		Connecting,
		Open,
		Closed,
		Failed,
	};

	enum class SocketEvent : u8 {
		Opened,
		Readable,
		Closed,
		Failed,
	};

	// invoked from Dispatch (never from network thread)
	using SocketCallback = std::function<void(SocketEvent event)>;

	struct SocketCore;

	class SHALLTEAR_NETWORK_API TCPSocket {
	public:
		explicit TCPSocket(std::shared_ptr<SocketCore> core);
		~TCPSocket();
		TCPSocket(const TCPSocket&) = delete;
		TCPSocket& operator=(const TCPSocket&) = delete;

		// host resolved/connected in background
		static std::unique_ptr<TCPSocket> Connect(std::string_view host, u16 port);
		static std::unique_ptr<TCPSocket> Connect(const Address& address);

		SocketState GetState() const;
		// bytes taken, fewer while send buffer is full
		size_t Send(std::span<const u8> data);
		// bytes read, 0 when none are waiting
		size_t Receive(std::span<u8> out);
		void Close();

		str GetError() const;
		Address GetPeerAddress() const;
		void SetNoDelay(bool noDelay);
		void SetCallback(SocketCallback callback);

		// connection moved out, socket left empty
		std::shared_ptr<SocketCore> Detach();
	private:
		std::shared_ptr<SocketCore> _core;
	};

	class SHALLTEAR_NETWORK_API TCPListener {
	public:
		explicit TCPListener(std::shared_ptr<SocketCore> core);
		~TCPListener();
		TCPListener(const TCPListener&) = delete;
		TCPListener& operator=(const TCPListener&) = delete;

		// port 0 for free port, Readable while connection is pending
		static std::unique_ptr<TCPListener> Listen(const Address& at, str* outError = nullptr, u32 backlog = 16);

		u16 GetPort() const;
		// null when no connection is waiting
		std::unique_ptr<TCPSocket> Accept();
		void Close();
		void SetCallback(SocketCallback callback);
	private:
		std::shared_ptr<SocketCore> _core;
	};

	class SHALLTEAR_NETWORK_API UDPSocket {
	public:
		explicit UDPSocket(std::shared_ptr<SocketCore> core);
		~UDPSocket();
		UDPSocket(const UDPSocket&) = delete;
		UDPSocket& operator=(const UDPSocket&) = delete;

		// port 0 for free port
		static std::unique_ptr<UDPSocket> Bind(const Address& at, str* outError = nullptr);

		u16 GetPort() const;
		bool SendTo(const Address& to, std::span<const u8> datagram);
		// datagram size, 0 when none is waiting, cut to out when longer
		size_t ReceiveFrom(std::span<u8> out, Address& from);
		void Close();
		str GetError() const;
		void SetCallback(SocketCallback callback);
	private:
		std::shared_ptr<SocketCore> _core;
	};

	// called each frame by package's stage
	SHALLTEAR_NETWORK_API void Dispatch();

} // namespace Shalltear::Net

#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/Address.h"

#include <span>
#include <string_view>

namespace Shalltear::Net::SocketBackend {

	using Handle = i64;
	inline constexpr Handle INVALID = -1;

	enum class Result : u8 {
		Done,
		WouldBlock,
		Closed,
		Error,
	};

	bool Startup(str& error);
	Handle Open(bool stream, bool v6, str& error);
	void Close(Handle handle);

	Result Connect(Handle handle, const Address& to, str& error);
	Result ConnectResult(Handle handle, str& error);
	Result Send(Handle handle, std::span<const u8> data, size_t& sent, str& error);
	Result Receive(Handle handle, std::span<u8> out, size_t& received, str& error);

	bool Bind(Handle handle, const Address& at, str& error);
	bool Listen(Handle handle, u32 backlog, str& error);
	Result Accept(Handle handle, Handle& accepted, Address& peer, str& error);

	Result SendTo(Handle handle, const Address& to, std::span<const u8> datagram, str& error);
	Result ReceiveFrom(Handle handle, std::span<u8> out, size_t& received, Address& from, str& error);

	bool LocalAddress(Handle handle, Address& out);
	bool PeerAddress(Handle handle, Address& out);
	void SetNoDelay(Handle handle, bool on);

	struct PollItem {
		Handle handle = INVALID;
		bool readable = false;
		bool failed = false;
	};
	void PollReadable(std::span<PollItem> items);

	bool Resolve(std::string_view host, u16 port, Vector<Address>& out, str& error);
	bool ParseIP(std::string_view text, Address& out);
	str FormatIP(const Address& address);

} // namespace Shalltear::Net::SocketBackend

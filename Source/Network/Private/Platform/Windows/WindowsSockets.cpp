#include "SocketBackend.h"

#include "Shalltear/Core/StringUtil.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <mutex>

namespace Shalltear::Net::SocketBackend {

	namespace {

		SOCKET ToSocket(Handle handle) {
			return static_cast<SOCKET>(handle);
		}

		Handle FromSocket(SOCKET socket) {
			return socket == INVALID_SOCKET ? INVALID : static_cast<Handle>(socket);
		}

		str ErrorText(int code) {
			wchar_t* buffer = nullptr;
			const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
												static_cast<DWORD>(code), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
			str text = length > 0 ? StringUtil::WideToUTF8(std::wstring_view(buffer, length)) : str{};
			if (buffer) {
				LocalFree(buffer);
			}
			while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.' || text.back() == ' ')) {
				text.pop_back();
			}
			return std::format("{} ({})", text.empty() ? "socket error" : text, code);
		}

		str LastError() {
			return ErrorText(WSAGetLastError());
		}

		int ToSockaddr(const Address& address, sockaddr_storage& out) {
			std::memset(&out, 0, sizeof(out));
			if (address.v6) {
				sockaddr_in6 in{};
				in.sin6_family = AF_INET6;
				in.sin6_port = htons(address.port);
				std::memcpy(&in.sin6_addr, address.bytes, 16);
				std::memcpy(&out, &in, sizeof(in));
				return static_cast<int>(sizeof(in));
			}
			sockaddr_in in{};
			in.sin_family = AF_INET;
			in.sin_port = htons(address.port);
			std::memcpy(&in.sin_addr, address.bytes, 4);
			std::memcpy(&out, &in, sizeof(in));
			return static_cast<int>(sizeof(in));
		}

		bool FromSockaddr(const sockaddr* from, Address& out) {
			out = Address{};
			if (from->sa_family == AF_INET6) {
				sockaddr_in6 in{};
				std::memcpy(&in, from, sizeof(in));
				out.v6 = true;
				out.port = ntohs(in.sin6_port);
				std::memcpy(out.bytes, &in.sin6_addr, 16);
				return true;
			}
			if (from->sa_family == AF_INET) {
				sockaddr_in in{};
				std::memcpy(&in, from, sizeof(in));
				out.port = ntohs(in.sin_port);
				std::memcpy(out.bytes, &in.sin_addr, 4);
				return true;
			}
			return false;
		}

		bool ClosedBy(int code) {
			return code == WSAECONNRESET || code == WSAECONNABORTED || code == WSAESHUTDOWN || code == WSAENETRESET;
		}

	} // anonymous namespace

	bool Startup(str& error) {
		static std::once_flag once;
		static int result = 0;
		std::call_once(once, [] {
			WSADATA data{};
			result = WSAStartup(MAKEWORD(2, 2), &data);
		});
		if (result != 0) {
			error = ErrorText(result);
			return false;
		}
		return true;
	}

	Handle Open(bool stream, bool v6, str& error) {
		if (!Startup(error)) {
			return INVALID;
		}
		const SOCKET socket = WSASocketW(v6 ? AF_INET6 : AF_INET, stream ? SOCK_STREAM : SOCK_DGRAM, stream ? IPPROTO_TCP : IPPROTO_UDP,
										 nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
		if (socket == INVALID_SOCKET) {
			error = LastError();
			return INVALID;
		}
		u_long nonBlocking = 1;
		ioctlsocket(socket, FIONBIO, &nonBlocking);
		if (v6) {
			DWORD only = 0;
			setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof(only));
		}
		if (!stream) {
			// no WSAECONNRESET on receive after ICMP port unreachable
			BOOL report = FALSE;
			DWORD bytes = 0;
			WSAIoctl(socket, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &bytes, nullptr, nullptr);
		}
		return FromSocket(socket);
	}

	void Close(Handle handle) {
		if (handle != INVALID) {
			closesocket(ToSocket(handle));
		}
	}

	Result Connect(Handle handle, const Address& to, str& error) {
		sockaddr_storage storage{};
		const int length = ToSockaddr(to, storage);
		if (connect(ToSocket(handle), reinterpret_cast<const sockaddr*>(&storage), length) == 0) {
			return Result::Done;
		}
		const int code = WSAGetLastError();
		if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS) {
			return Result::WouldBlock;
		}
		error = ErrorText(code);
		return Result::Error;
	}

	Result ConnectResult(Handle handle, str& error) {
		const SOCKET socket = ToSocket(handle);
		fd_set writable;
		fd_set failed;
		FD_ZERO(&writable);
		FD_ZERO(&failed);
		FD_SET(socket, &writable);
		FD_SET(socket, &failed);
		timeval zero{};
		const int ready = select(0, nullptr, &writable, &failed, &zero);
		if (ready == SOCKET_ERROR) {
			error = LastError();
			return Result::Error;
		}
		if (ready == 0) {
			return Result::WouldBlock;
		}
		if (FD_ISSET(socket, &failed)) {
			int code = 0;
			int length = sizeof(code);
			getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &length);
			error = ErrorText(code != 0 ? code : WSAECONNREFUSED);
			return Result::Error;
		}
		return Result::Done;
	}

	Result Send(Handle handle, std::span<const u8> data, size_t& sent, str& error) {
		sent = 0;
		if (data.empty()) {
			return Result::Done;
		}
		const int size = static_cast<int>(std::min<size_t>(data.size(), INT_MAX));
		const int written = send(ToSocket(handle), reinterpret_cast<const char*>(data.data()), size, 0);
		if (written >= 0) {
			sent = static_cast<size_t>(written);
			return Result::Done;
		}
		const int code = WSAGetLastError();
		if (code == WSAEWOULDBLOCK) {
			return Result::WouldBlock;
		}
		error = ErrorText(code);
		return ClosedBy(code) ? Result::Closed : Result::Error;
	}

	Result Receive(Handle handle, std::span<u8> out, size_t& received, str& error) {
		received = 0;
		if (out.empty()) {
			return Result::Done;
		}
		const int size = static_cast<int>(std::min<size_t>(out.size(), INT_MAX));
		const int read = recv(ToSocket(handle), reinterpret_cast<char*>(out.data()), size, 0);
		if (read > 0) {
			received = static_cast<size_t>(read);
			return Result::Done;
		}
		if (read == 0) {
			return Result::Closed;
		}
		const int code = WSAGetLastError();
		if (code == WSAEWOULDBLOCK) {
			return Result::WouldBlock;
		}
		error = ErrorText(code);
		return ClosedBy(code) ? Result::Closed : Result::Error;
	}

	bool Bind(Handle handle, const Address& at, str& error) {
		sockaddr_storage storage{};
		const int length = ToSockaddr(at, storage);
		if (bind(ToSocket(handle), reinterpret_cast<const sockaddr*>(&storage), length) != 0) {
			error = LastError();
			return false;
		}
		return true;
	}

	bool Listen(Handle handle, u32 backlog, str& error) {
		if (listen(ToSocket(handle), static_cast<int>(std::min<u32>(backlog, SOMAXCONN))) != 0) {
			error = LastError();
			return false;
		}
		return true;
	}

	Result Accept(Handle handle, Handle& accepted, Address& peer, str& error) {
		accepted = INVALID;
		sockaddr_storage storage{};
		int length = sizeof(storage);
		const SOCKET socket = accept(ToSocket(handle), reinterpret_cast<sockaddr*>(&storage), &length);
		if (socket == INVALID_SOCKET) {
			const int code = WSAGetLastError();
			if (code == WSAEWOULDBLOCK || code == WSAECONNRESET) {
				return Result::WouldBlock;
			}
			error = ErrorText(code);
			return Result::Error;
		}
		u_long nonBlocking = 1;
		ioctlsocket(socket, FIONBIO, &nonBlocking);
		FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), peer);
		accepted = FromSocket(socket);
		return Result::Done;
	}

	Result SendTo(Handle handle, const Address& to, std::span<const u8> datagram, str& error) {
		sockaddr_storage storage{};
		const int length = ToSockaddr(to, storage);
		const int size = static_cast<int>(std::min<size_t>(datagram.size(), INT_MAX));
		if (sendto(ToSocket(handle), reinterpret_cast<const char*>(datagram.data()), size, 0, reinterpret_cast<const sockaddr*>(&storage),
				   length) >= 0) {
			return Result::Done;
		}
		const int code = WSAGetLastError();
		if (code == WSAEWOULDBLOCK) {
			return Result::WouldBlock;
		}
		error = ErrorText(code);
		return Result::Error;
	}

	Result ReceiveFrom(Handle handle, std::span<u8> out, size_t& received, Address& from, str& error) {
		received = 0;
		sockaddr_storage storage{};
		int length = sizeof(storage);
		const int size = static_cast<int>(std::min<size_t>(out.size(), INT_MAX));
		const int read = recvfrom(ToSocket(handle), reinterpret_cast<char*>(out.data()), size, 0, reinterpret_cast<sockaddr*>(&storage), &length);
		if (read >= 0) {
			received = static_cast<size_t>(read);
			FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), from);
			return Result::Done;
		}
		const int code = WSAGetLastError();
		if (code == WSAEMSGSIZE) {
			received = out.size();
			FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), from);
			return Result::Done;
		}
		if (code == WSAEWOULDBLOCK || code == WSAECONNRESET) {
			return Result::WouldBlock;
		}
		error = ErrorText(code);
		return Result::Error;
	}

	bool LocalAddress(Handle handle, Address& out) {
		sockaddr_storage storage{};
		int length = sizeof(storage);
		return getsockname(ToSocket(handle), reinterpret_cast<sockaddr*>(&storage), &length) == 0 &&
			   FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), out);
	}

	bool PeerAddress(Handle handle, Address& out) {
		sockaddr_storage storage{};
		int length = sizeof(storage);
		return getpeername(ToSocket(handle), reinterpret_cast<sockaddr*>(&storage), &length) == 0 &&
			   FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), out);
	}

	void SetNoDelay(Handle handle, bool on) {
		const BOOL value = on ? TRUE : FALSE;
		setsockopt(ToSocket(handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&value), sizeof(value));
	}

	void PollReadable(std::span<PollItem> items) {
		if (items.empty()) {
			return;
		}
		Vector<WSAPOLLFD> fds{GetMemoryResource(MemTag::Platform)};
		fds.resize(items.size());
		for (size_t i = 0; i < items.size(); ++i) {
			fds[i].fd = ToSocket(items[i].handle);
			fds[i].events = POLLRDNORM;
			fds[i].revents = 0;
		}
		if (WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), 0) == SOCKET_ERROR) {
			return;
		}
		for (size_t i = 0; i < items.size(); ++i) {
			const SHORT revents = fds[i].revents;
			items[i].readable = (revents & (POLLRDNORM | POLLHUP | POLLERR)) != 0;
			items[i].failed = (revents & (POLLERR | POLLNVAL)) != 0;
		}
	}

	bool Resolve(std::string_view host, u16 port, Vector<Address>& out, str& error) {
		out.clear();
		if (!Startup(error)) {
			return false;
		}
		const std::wstring wideHost = StringUtil::UTF8ToWide(host);
		const std::wstring wideService = std::to_wstring(port);
		ADDRINFOW hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG;
		ADDRINFOW* found = nullptr;
		const int code = GetAddrInfoW(wideHost.c_str(), wideService.c_str(), &hints, &found);
		if (code != 0) {
			error = ErrorText(code);
			return false;
		}
		for (const ADDRINFOW* entry = found; entry; entry = entry->ai_next) {
			Address address;
			if (entry->ai_addr && FromSockaddr(entry->ai_addr, address)) {
				out.push_back(address);
			}
		}
		FreeAddrInfoW(found);
		if (out.empty()) {
			error = std::format("no address for '{}'", host);
			return false;
		}
		return true;
	}

	bool ParseIP(std::string_view text, Address& out) {
		const str copy(text);
		Address parsed;
		if (InetPtonA(AF_INET, copy.c_str(), parsed.bytes) == 1) {
			parsed.v6 = false;
		} else if (InetPtonA(AF_INET6, copy.c_str(), parsed.bytes) == 1) {
			parsed.v6 = true;
		} else {
			return false;
		}
		parsed.port = out.port;
		out = parsed;
		return true;
	}

	str FormatIP(const Address& address) {
		char buffer[INET6_ADDRSTRLEN] = {};
		in6_addr copy{};
		std::memcpy(&copy, address.bytes, address.v6 ? 16 : 4);
		if (!InetNtopA(address.v6 ? AF_INET6 : AF_INET, &copy, buffer, sizeof(buffer))) {
			return {};
		}
		return str(buffer);
	}

} // namespace Shalltear::Net::SocketBackend

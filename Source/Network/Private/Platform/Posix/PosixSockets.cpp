#include "SocketBackend.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Shalltear::Net::SocketBackend {

	namespace {

		int ToFd(Handle handle) {
			return static_cast<int>(handle);
		}

		str ErrorText(int code) {
			return std::format("{} ({})", std::strerror(code), code);
		}

		socklen_t ToSockaddr(const Address& address, sockaddr_storage& out) {
			std::memset(&out, 0, sizeof(out));
			if (address.v6) {
				sockaddr_in6 in{};
				in.sin6_family = AF_INET6;
				in.sin6_port = htons(address.port);
				std::memcpy(&in.sin6_addr, address.bytes, 16);
				std::memcpy(&out, &in, sizeof(in));
				return sizeof(in);
			}
			sockaddr_in in{};
			in.sin_family = AF_INET;
			in.sin_port = htons(address.port);
			std::memcpy(&in.sin_addr, address.bytes, 4);
			std::memcpy(&out, &in, sizeof(in));
			return sizeof(in);
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

		bool Blocked(int code) {
			return code == EAGAIN || code == EWOULDBLOCK;
		}

		bool ClosedBy(int code) {
			return code == ECONNRESET || code == EPIPE || code == ECONNABORTED || code == ENOTCONN;
		}

		void MakeNonBlocking(int fd) {
			const int flags = fcntl(fd, F_GETFL, 0);
			fcntl(fd, F_SETFL, flags | O_NONBLOCK);
			fcntl(fd, F_SETFD, FD_CLOEXEC);
		}

	} // anonymous namespace

	bool Startup(str&) {
		return true;
	}

	Handle Open(bool stream, bool v6, str& error) {
		const int fd = ::socket(v6 ? AF_INET6 : AF_INET, stream ? SOCK_STREAM : SOCK_DGRAM, stream ? IPPROTO_TCP : IPPROTO_UDP);
		if (fd < 0) {
			error = ErrorText(errno);
			return INVALID;
		}
		MakeNonBlocking(fd);
		if (v6) {
			const int only = 0;
			setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));
		}
		return fd;
	}

	void Close(Handle handle) {
		if (handle != INVALID) {
			::close(ToFd(handle));
		}
	}

	Result Connect(Handle handle, const Address& to, str& error) {
		sockaddr_storage storage{};
		const socklen_t length = ToSockaddr(to, storage);
		if (::connect(ToFd(handle), reinterpret_cast<const sockaddr*>(&storage), length) == 0) {
			return Result::Done;
		}
		if (errno == EINPROGRESS || Blocked(errno)) {
			return Result::WouldBlock;
		}
		error = ErrorText(errno);
		return Result::Error;
	}

	Result ConnectResult(Handle handle, str& error) {
		pollfd fd{};
		fd.fd = ToFd(handle);
		fd.events = POLLOUT;
		const int ready = ::poll(&fd, 1, 0);
		if (ready < 0) {
			error = ErrorText(errno);
			return Result::Error;
		}
		if (ready == 0) {
			return Result::WouldBlock;
		}
		int code = 0;
		socklen_t length = sizeof(code);
		getsockopt(fd.fd, SOL_SOCKET, SO_ERROR, &code, &length);
		if (code != 0) {
			error = ErrorText(code);
			return Result::Error;
		}
		return Result::Done;
	}

	Result Send(Handle handle, std::span<const u8> data, size_t& sent, str& error) {
		sent = 0;
		if (data.empty()) {
			return Result::Done;
		}
		const ssize_t written = ::send(ToFd(handle), data.data(), data.size(), MSG_NOSIGNAL);
		if (written >= 0) {
			sent = static_cast<size_t>(written);
			return Result::Done;
		}
		if (Blocked(errno)) {
			return Result::WouldBlock;
		}
		error = ErrorText(errno);
		return ClosedBy(errno) ? Result::Closed : Result::Error;
	}

	Result Receive(Handle handle, std::span<u8> out, size_t& received, str& error) {
		received = 0;
		if (out.empty()) {
			return Result::Done;
		}
		const ssize_t read = ::recv(ToFd(handle), out.data(), out.size(), 0);
		if (read > 0) {
			received = static_cast<size_t>(read);
			return Result::Done;
		}
		if (read == 0) {
			return Result::Closed;
		}
		if (Blocked(errno)) {
			return Result::WouldBlock;
		}
		error = ErrorText(errno);
		return ClosedBy(errno) ? Result::Closed : Result::Error;
	}

	bool Bind(Handle handle, const Address& at, str& error) {
		const int reuse = 1;
		setsockopt(ToFd(handle), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
		sockaddr_storage storage{};
		const socklen_t length = ToSockaddr(at, storage);
		if (::bind(ToFd(handle), reinterpret_cast<const sockaddr*>(&storage), length) != 0) {
			error = ErrorText(errno);
			return false;
		}
		return true;
	}

	bool Listen(Handle handle, u32 backlog, str& error) {
		if (::listen(ToFd(handle), static_cast<int>(backlog)) != 0) {
			error = ErrorText(errno);
			return false;
		}
		return true;
	}

	Result Accept(Handle handle, Handle& accepted, Address& peer, str& error) {
		accepted = INVALID;
		sockaddr_storage storage{};
		socklen_t length = sizeof(storage);
		const int fd = ::accept(ToFd(handle), reinterpret_cast<sockaddr*>(&storage), &length);
		if (fd < 0) {
			if (Blocked(errno) || errno == ECONNABORTED || errno == EINTR) {
				return Result::WouldBlock;
			}
			error = ErrorText(errno);
			return Result::Error;
		}
		MakeNonBlocking(fd);
		FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), peer);
		accepted = fd;
		return Result::Done;
	}

	Result SendTo(Handle handle, const Address& to, std::span<const u8> datagram, str& error) {
		sockaddr_storage storage{};
		const socklen_t length = ToSockaddr(to, storage);
		if (::sendto(ToFd(handle), datagram.data(), datagram.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&storage), length) >= 0) {
			return Result::Done;
		}
		if (Blocked(errno)) {
			return Result::WouldBlock;
		}
		error = ErrorText(errno);
		return Result::Error;
	}

	Result ReceiveFrom(Handle handle, std::span<u8> out, size_t& received, Address& from, str& error) {
		received = 0;
		sockaddr_storage storage{};
		socklen_t length = sizeof(storage);
		const ssize_t read = ::recvfrom(ToFd(handle), out.data(), out.size(), 0, reinterpret_cast<sockaddr*>(&storage), &length);
		if (read >= 0) {
			received = static_cast<size_t>(read);
			FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), from);
			return Result::Done;
		}
		if (Blocked(errno) || errno == ECONNREFUSED) {
			return Result::WouldBlock;
		}
		error = ErrorText(errno);
		return Result::Error;
	}

	bool LocalAddress(Handle handle, Address& out) {
		sockaddr_storage storage{};
		socklen_t length = sizeof(storage);
		return ::getsockname(ToFd(handle), reinterpret_cast<sockaddr*>(&storage), &length) == 0 &&
			   FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), out);
	}

	bool PeerAddress(Handle handle, Address& out) {
		sockaddr_storage storage{};
		socklen_t length = sizeof(storage);
		return ::getpeername(ToFd(handle), reinterpret_cast<sockaddr*>(&storage), &length) == 0 &&
			   FromSockaddr(reinterpret_cast<const sockaddr*>(&storage), out);
	}

	void SetNoDelay(Handle handle, bool on) {
		const int value = on ? 1 : 0;
		setsockopt(ToFd(handle), IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
	}

	void PollReadable(std::span<PollItem> items) {
		if (items.empty()) {
			return;
		}
		Vector<pollfd> fds{GetMemoryResource(MemTag::Platform)};
		fds.resize(items.size());
		for (size_t i = 0; i < items.size(); ++i) {
			fds[i].fd = ToFd(items[i].handle);
			fds[i].events = POLLIN;
			fds[i].revents = 0;
		}
		if (::poll(fds.data(), fds.size(), 0) < 0) {
			return;
		}
		for (size_t i = 0; i < items.size(); ++i) {
			const short revents = fds[i].revents;
			items[i].readable = (revents & (POLLIN | POLLHUP | POLLERR)) != 0;
			items[i].failed = (revents & (POLLERR | POLLNVAL)) != 0;
		}
	}

	bool Resolve(std::string_view host, u16 port, Vector<Address>& out, str& error) {
		out.clear();
		const str name(host);
		const str service = std::to_string(port);
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG;
		addrinfo* found = nullptr;
		const int code = ::getaddrinfo(name.c_str(), service.c_str(), &hints, &found);
		if (code != 0) {
			error = std::format("{} ({})", gai_strerror(code), code);
			return false;
		}
		for (const addrinfo* entry = found; entry; entry = entry->ai_next) {
			Address address;
			if (entry->ai_addr && FromSockaddr(entry->ai_addr, address)) {
				out.push_back(address);
			}
		}
		::freeaddrinfo(found);
		if (out.empty()) {
			error = std::format("no address for '{}'", host);
			return false;
		}
		return true;
	}

	bool ParseIP(std::string_view text, Address& out) {
		const str copy(text);
		Address parsed;
		if (::inet_pton(AF_INET, copy.c_str(), parsed.bytes) == 1) {
			parsed.v6 = false;
		} else if (::inet_pton(AF_INET6, copy.c_str(), parsed.bytes) == 1) {
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
		if (!::inet_ntop(address.v6 ? AF_INET6 : AF_INET, address.bytes, buffer, sizeof(buffer))) {
			return {};
		}
		return str(buffer);
	}

} // namespace Shalltear::Net::SocketBackend

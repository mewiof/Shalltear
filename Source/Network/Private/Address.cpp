#include "Shalltear/Network/Address.h"

#include "NetworkInternal.h"
#include "SocketBackend.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>

namespace Shalltear::Net {

	namespace {

		bool ParsePort(std::string_view text, u16& out) {
			if (text.empty() || text.size() > 5) {
				return false;
			}
			u32 value = 0;
			const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (failure != std::errc{} || end != text.data() + text.size() || value > 65535) {
				return false;
			}
			out = static_cast<u16>(value);
			return true;
		}

	} // anonymous namespace

	Address Address::Loopback(u16 port, bool v6) {
		Address address;
		address.port = port;
		address.v6 = v6;
		if (v6) {
			address.bytes[15] = 1;
		} else {
			address.bytes[0] = 127;
			address.bytes[3] = 1;
		}
		return address;
	}

	Address Address::Any(u16 port, bool v6) {
		Address address;
		address.port = port;
		address.v6 = v6;
		return address;
	}

	bool Address::Parse(std::string_view text, Address& out) {
		std::string_view host = text;
		u16 port = 0;
		if (text.starts_with('[')) {
			const size_t close = text.find(']');
			if (close == std::string_view::npos) {
				return false;
			}
			host = text.substr(1, close - 1);
			const std::string_view rest = text.substr(close + 1);
			if (!rest.empty() && (rest.front() != ':' || !ParsePort(rest.substr(1), port))) {
				return false;
			}
		} else if (std::count(text.begin(), text.end(), ':') == 1) {
			const size_t colon = text.find(':');
			host = text.substr(0, colon);
			if (!ParsePort(text.substr(colon + 1), port)) {
				return false;
			}
		}
		Address parsed;
		parsed.port = port;
		if (host.empty() || !SocketBackend::ParseIP(host, parsed)) {
			return false;
		}
		out = parsed;
		return true;
	}

	str Address::ToString() const {
		const str ip = SocketBackend::FormatIP(*this);
		return v6 ? std::format("[{}]:{}", ip, port) : std::format("{}:{}", ip, port);
	}

	bool Address::operator==(const Address& other) const {
		return v6 == other.v6 && port == other.port && std::memcmp(bytes, other.bytes, v6 ? 16 : 4) == 0;
	}

	Address MapToV6(const Address& address) {
		if (address.v6) {
			return address;
		}
		Address mapped;
		mapped.v6 = true;
		mapped.port = address.port;
		mapped.bytes[10] = 0xFF;
		mapped.bytes[11] = 0xFF;
		std::memcpy(mapped.bytes + 12, address.bytes, 4);
		return mapped;
	}

	Address UnmapV4(const Address& address) {
		static constexpr u8 PREFIX[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
		if (!address.v6 || std::memcmp(address.bytes, PREFIX, sizeof(PREFIX)) != 0) {
			return address;
		}
		Address plain;
		plain.port = address.port;
		std::memcpy(plain.bytes, address.bytes + 12, 4);
		return plain;
	}

} // namespace Shalltear::Net

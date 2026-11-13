#pragma once

#include "Shalltear/Core/Types.h"
#include "Shalltear/Network/NetworkAPI.h"

#include <string_view>

namespace Shalltear::Net {

	struct SHALLTEAR_NETWORK_API Address {
		// network byte order, IPv4 in bytes 0-3
		u8 bytes[16] = {};
		u16 port = 0;
		bool v6 = false;

		static Address Loopback(u16 port, bool v6 = false);
		static Address Any(u16 port, bool v6 = false);
		// "1.2.3.4:80", "[::1]:80", or bare address for port 0
		static bool Parse(std::string_view text, Address& out);
		str ToString() const;

		bool operator==(const Address& other) const;
	};

} // namespace Shalltear::Net

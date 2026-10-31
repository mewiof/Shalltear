#pragma once

#include <cstdint>
#include <charconv>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace Shalltear {

	using u8 = uint8_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
	using u64 = uint64_t;
	using i8 = int8_t;
	using i16 = int16_t;
	using i32 = int32_t;
	using i64 = int64_t;
	using f32 = float;
	using f64 = double;

	using str = std::string;

	using namespace std::string_literals;

	struct Guid {
		u64 low = 0;
		u64 high = 0;

		constexpr Guid() = default;
		constexpr Guid(u64 lowPart, u64 highPart) : low(lowPart), high(highPart) {}

		constexpr bool operator==(const Guid& o) const { return low == o.low && high == o.high; }
		constexpr bool operator!=(const Guid& o) const { return !(*this == o); }

		constexpr bool operator<(const Guid& o) const {
			return (high < o.high) || (high == o.high && low < o.low);
		}

		constexpr bool IsValid() const { return low != 0 || high != 0; }

		str ToString() const {
			return std::format("{:016x}-{:016x}", low, high);
		}

		static Guid FromString(std::string_view text) {
			if (text.size() != 33 || text[16] != '-') {
				return Guid{};
			}

			u64 lowPart = 0;
			u64 highPart = 0;
			const char* lowBegin = text.data();
			const char* highBegin = text.data() + 17;

			auto lowResult = std::from_chars(lowBegin, lowBegin + 16, lowPart, 16);
			auto highResult = std::from_chars(highBegin, highBegin + 16, highPart, 16);

			if (lowResult.ec != std::errc{} || lowResult.ptr != lowBegin + 16 ||
				highResult.ec != std::errc{} || highResult.ptr != highBegin + 16) {
				return Guid{};
			}

			return Guid{lowPart, highPart};
		}
	};

} // namespace Shalltear

template <>
struct std::hash<Shalltear::Guid> {
	std::size_t operator()(const Shalltear::Guid& guid) const noexcept {
		std::size_t h = static_cast<std::size_t>(guid.low);
		h ^= static_cast<std::size_t>(guid.high) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
		return h;
	}
};

template <>
struct std::formatter<Shalltear::Guid> : std::formatter<std::string> {
	auto format(const Shalltear::Guid& guid, std::format_context& ctx) const {
		return std::formatter<std::string>::format(guid.ToString(), ctx);
	}
};

#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API StringHash {
	public:
		constexpr StringHash() : _value(0) {}
		constexpr explicit StringHash(u64 value) : _value(value) {}

		constexpr StringHash(const char* str) : _value(CalculateFNV1a(str)) {}
		constexpr StringHash(std::string_view str) : _value(CalculateFNV1a(str.data(), str.size())) {}

		constexpr u64 GetValue() const { return _value; }

		constexpr bool operator==(const StringHash& other) const { return _value == other._value; }
		constexpr bool operator!=(const StringHash& other) const { return _value != other._value; }
		constexpr bool operator<(const StringHash& other) const { return _value < other._value; }
	private:
		u64 _value;

		static constexpr u64 PRIME = 0x00000100000001b3ULL;
		static constexpr u64 OFFSET_BASIS = 0xcbf29ce484222325ULL;

		static constexpr u64 CalculateFNV1a(const char* str) {
			u64 hash = OFFSET_BASIS;
			while (*str) {
				hash ^= static_cast<u64>(*str++);
				hash *= PRIME;
			}
			return hash;
		}
		static constexpr u64 CalculateFNV1a(const char* str, size_t length) {
			u64 hash = OFFSET_BASIS;
			for (size_t i = 0; i < length; i++) {
				hash ^= static_cast<u64>(str[i]);
				hash *= PRIME;
			}
			return hash;
		}
	};

	constexpr StringHash operator""_hash(const char* str, size_t size) {
		return StringHash(std::string_view(str, size));
	}
} //namespace Shalltear

template <>
struct std::hash<Shalltear::StringHash> {
	std::size_t operator()(const Shalltear::StringHash& hash) const noexcept {
		return static_cast<std::size_t>(hash.GetValue());
	}
};

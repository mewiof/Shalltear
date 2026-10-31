#pragma once

#include <type_traits>

#define SLT_ENUM_CLASS_FLAGS(EnumType)                                                                            \
	inline constexpr EnumType operator|(EnumType a, EnumType b) noexcept {                                        \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));                                      \
	}                                                                                                             \
	inline constexpr EnumType operator&(EnumType a, EnumType b) noexcept {                                        \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));                                      \
	}                                                                                                             \
	inline constexpr EnumType operator^(EnumType a, EnumType b) noexcept {                                        \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return static_cast<EnumType>(static_cast<U>(a) ^ static_cast<U>(b));                                      \
	}                                                                                                             \
	inline constexpr EnumType operator~(EnumType a) noexcept {                                                    \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return static_cast<EnumType>(~static_cast<U>(a));                                                         \
	}                                                                                                             \
	inline constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept { return a = a | b; }                 \
	inline constexpr EnumType& operator&=(EnumType& a, EnumType b) noexcept { return a = a & b; }                 \
	inline constexpr EnumType& operator^=(EnumType& a, EnumType b) noexcept { return a = a ^ b; }                 \
	inline constexpr bool HasFlag(EnumType value, EnumType flags) noexcept {                                      \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return (static_cast<U>(value) & static_cast<U>(flags)) == static_cast<U>(flags) && static_cast<U>(flags); \
	}                                                                                                             \
	inline constexpr bool HasAnyFlag(EnumType value, EnumType flags) noexcept {                                   \
		using U = std::underlying_type_t<EnumType>;                                                               \
		return (static_cast<U>(value) & static_cast<U>(flags)) != 0;                                              \
	}

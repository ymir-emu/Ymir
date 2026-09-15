#pragma once

/**
@file
@brief Bitwise operations and bit twiddling tricks.
*/

#include "inline.hpp"

#include <ymir/core/types.hpp>

#include <array>
#include <bit>
#include <climits>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace bit {

/// @brief Determines if `value` is a power of two.
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to check
/// @return `true` if `value` is a power of two
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr bool is_power_of_two(T value) noexcept {
    return std::popcount(value) == 1;
}

/// @brief Returns the next power of two not less than `value`.
/// @tparam T the type of the unsigned integral
/// @param[in] value the base value
/// @return `value` rounded up to the next power of two
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T next_power_of_two(T value) noexcept {
    return std::bit_ceil(value);
}

/// @brief Adjusts a value to the smallest value not less than `value` with the specified alignment
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to align
/// @param[in] alignment the alignment to align to
/// @return the aligned value
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T align(T value, unsigned alignment) noexcept {
    const T mod = static_cast<T>(value % alignment);
    value -= mod;
    return static_cast<T>(mod == T{0} ? value : value + alignment);
}

/// @brief Adjusts a value to the smallest value not less than `value` with the `B` least significant bits zeroed out.
/// @tparam[in] B the number of bits to align to
/// @tparam[in] T the type of the integral
/// @param[in] value the value to align
/// @return the aligned value
template <unsigned B, std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T align(T value) noexcept {
    constexpr T kOne = 1;
    constexpr T kShift = B;
    constexpr T kOffset = (kOne << B) - kOne;
    constexpr T kMask = ~kOffset;
    return (value + kOffset) & kMask;
}

/// @brief Sign-extends a `B`-bit integer from the least significant bits of `value`.
/// @tparam B the bit width of the value
/// @tparam T the type of the integral
/// @param[in] value the value to sign-extend
/// @return the signed version of the value, sign-extended from `B` bits to the bit width of `T`
template <unsigned B, std::integral T>
[[nodiscard]] FORCE_INLINE constexpr auto sign_extend(T value) noexcept {
    static_assert(B > 0, "cannot extract zero bits out of a value");
    static_assert(B <= sizeof(T) * CHAR_BIT, "B is too large for the given type");
    using ST = std::make_signed_t<T>;
    ST signedValue = value;
    signedValue <<= sizeof(T) * CHAR_BIT - B;
    signedValue >>= sizeof(T) * CHAR_BIT - B;
    return signedValue;
}

/// @brief Tests if the bit at `pos` is set in `value`.
/// @tparam pos the bit position, where 0 is the least significant bit; must not exceed the bit width of `T`
/// @tparam T the type of the integral
/// @param[in] value the value to test
/// @return `true` if the `pos`-th bit is set, `false` if clear
template <std::size_t pos, std::integral T>
[[nodiscard]] FORCE_INLINE constexpr bool test(T value) noexcept {
    static_assert(pos < sizeof(T) * 8, "pos out of range");
    return (value >> pos) & 1;
}

/// @brief Extracts the range of bits from `start` to `end` from `value`.
///
/// The range is inclusive on both `start` and `end`. Also, `start` must be less than or equal to `end` and neither may
/// exceed the bit width of `T`.
///
/// @tparam start the least significant bit of the range to extract.
/// @tparam end the most significant bit of the range to extract.
/// @tparam T the type of the integral
/// @param[in] value the value to extract bits from
/// @return the bits from `start` to `end` of `value`, shifted down to the zeroth position
template <std::size_t start, std::size_t end = start, std::integral T>
[[nodiscard]] FORCE_INLINE constexpr T extract(T value) noexcept {
    static_assert(start < sizeof(T) * 8, "start out of range");
    static_assert(end < sizeof(T) * 8, "end out of range");
    static_assert(end >= start, "end cannot be before start");

    using UT = std::make_unsigned_t<T>;

    constexpr std::size_t length = end - start;
    constexpr UT mask = static_cast<UT>(~(~UT{0} << length << 1));
    return (value >> start) & mask;
}

/// @brief Extracts a signed integer from the range of bits from `start` to `end` from `value`.
///
/// The range is inclusive on both `start` and `end`. Also, `start` must be less than or equal to `end` and neither may
/// exceed the bit width of `T`.
///
/// @tparam start the least significant bit of the range to extract.
/// @tparam end the most significant bit of the range to extract.
/// @tparam T the type of the integral
/// @param[in] value the value to extract bits from
/// @return the signed integer contained in the bit range between `start` and `end` of `value`
template <std::size_t start, std::size_t end = start, std::integral T>
[[nodiscard]] FORCE_INLINE auto extract_signed(T value) noexcept {
    return sign_extend<end - start + 1>(extract<start, end>(value));
}

/// @brief Deposits the least significant bits from `value` into the bit range between `start` and `end` of `base` and
/// returns the modified value.
///
/// The range is inclusive on both `start` and `end`. Also, `start` must be less than or equal to `end` and neither may
/// exceed the bit width of `T`.
///
/// @tparam start the least significant bit of the range to extract.
/// @tparam end the most significant bit of the range to extract.
/// @tparam T the type of `base`
/// @tparam TV the type of `value`
/// @param[in] base the value to deposit bits into
/// @param[in] value the value to extract bits from
/// @return `base` with the bits between `start` and `end` replaced with the least significant bits of `value`
template <std::size_t start, std::size_t end = start, std::integral T, std::integral TV = T>
[[nodiscard]] FORCE_INLINE constexpr T deposit(T base, TV value) noexcept {
    static_assert(start < sizeof(T) * 8, "start out of range");
    static_assert(end < sizeof(T) * 8, "end out of range");
    static_assert(end >= start, "end cannot be before start");

    using UT = std::make_unsigned_t<T>;

    constexpr std::size_t length = end - start;
    constexpr UT mask = static_cast<UT>(~(~UT{0} << length << 1));
    base &= ~(mask << start);
    base |= (value & mask) << start;
    return base;
}

/// @brief Deposits the least significant bits from `value` into the bit range between `start` and `end` of `dest`.
///
/// The range is inclusive on both `start` and `end`. Also, `start` must be less than or equal to `end` and neither may
/// exceed the bit width of `T`.
///
/// @tparam start the least significant bit of the range to extract.
/// @tparam end the most significant bit of the range to extract.
/// @tparam T the type of `base`
/// @tparam TV the type of `value`
/// @param[in,out] dest the value to deposit bits into
/// @param[in] value the value to extract bits from
template <std::size_t start, std::size_t end = start, std::integral T, std::integral TV = T>
FORCE_INLINE constexpr void deposit_into(T &dest, TV value) noexcept {
    dest = deposit<start, end>(dest, value);
}

/// @brief Compresses (gathers) the masked bits of `value` into the least significant bits of the output.
///
/// @tparam mask the bits to gather
/// @tparam T the type of the integral
/// @param[in] value the value to extract bits from
/// @return the bits of value selected by `mask`, gathered into the least significant bits
template <std::size_t mask, std::integral T>
[[nodiscard]] FORCE_INLINE constexpr T gather(T value) noexcept {
    constexpr auto kTMask = static_cast<T>(mask);
    // Hacker's Delight, 2nd edition, page 153
    value &= kTMask;                     // Clear irrelevant bits
    T mk = ~kTMask << static_cast<T>(1); // We will count 0s to the right
    T m = kTMask;

    constexpr T iters = std::countr_zero(sizeof(T)) + 3;
    for (T i = 0; i < iters; i++) {
        T mp = mk ^ (mk << 1); // Parallel suffix
        mp ^= mp << 2;
        mp ^= mp << 4;
        for (T j = 3; j < iters; j++) {
            mp ^= mp << ((T)1 << j);
        }

        T mv = mp & m; // Bits to move

        m = (m ^ mv) | (mv >> ((T)1 << i)); // Compress m
        T t = value & mv;
        value = (value ^ t) | (t >> ((T)1 << i)); // Compress x
        mk &= ~mp;
    }
    return value;
}

/// @brief Expands (scatters) the least significant bits of `value` into the places selected by `mask`.
///
/// @tparam mask the bits to scatter the value into
/// @tparam T the type of the integral
/// @param[in] value the value to extract bits from
/// @return the least significant bits of `value` scattered into the `mask` bits
template <std::size_t mask, std::integral T>
[[nodiscard]] FORCE_INLINE constexpr T scatter(T value) noexcept {
    // Hacker's Delight, 2nd edition, page 157
    T m0 = mask;                       // Save original mask
    T mk = static_cast<T>(~mask << 1); // We will count 0s to the right
    T m = mask;

    constexpr T iters = std::countr_zero(sizeof(T)) + 3;
    std::array<T, iters> array{};
    for (T i = 0; i < iters; i++) {
        T mp = mk ^ (mk << 1); // Parallel suffix
        mp ^= mp << 2;
        mp ^= mp << 4;
        for (T j = 3; j < iters; j++) {
            mp ^= mp << ((T)1 << j);
        }

        T mv = mp & m; // Bits to move

        array[i] = mv;

        m = (m ^ mv) | (mv >> ((T)1 << i)); // Compress m
        mk &= ~mp;
    }

    for (T i = iters - 1; i >= 0 && i < iters; i--) {
        T mv = array[i];
        T t = value << ((T)1 << i);
        value = (value & ~mv) | (t & mv);
    }

    return value & m0; // Clear out extraneous bits
}

namespace detail {

    template <class T, std::size_t... N>
    [[nodiscard]] FORCE_INLINE constexpr T byte_swap_impl(T value, std::index_sequence<N...>) noexcept {
        return ((((value >> (N * CHAR_BIT)) & (T)(unsigned char)(-1)) << ((sizeof(T) - 1 - N) * CHAR_BIT)) | ...);
    };

} // namespace detail

/// @brief Swaps the bytes of `value`.
///
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to byte swap
/// @return `value` with its bytes swapped
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T byte_swap(T value) noexcept {
    return detail::byte_swap_impl<T>(value, std::make_index_sequence<sizeof(T)>{});
}

#if defined(__clang__) || defined(__GNUC__)
template <>
[[nodiscard]] FORCE_INLINE constexpr uint64 byte_swap<uint64>(uint64 value) noexcept {
    return __builtin_bswap64(value);
}

template <>
[[nodiscard]] FORCE_INLINE constexpr uint32 byte_swap<uint32>(uint32 value) noexcept {
    return __builtin_bswap32(value);
}

template <>
[[nodiscard]] FORCE_INLINE constexpr uint16 byte_swap<uint16>(uint16 value) noexcept {
    return __builtin_bswap16(value);
}
#endif

/// @brief Swaps the bytes of `value` if `endianness` doesn't match the native endianness.
///
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to byte swap
/// @return `value` with its bytes swapped if `endianess` is not native
template <std::endian endianness, std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T endian_swap(T value) noexcept {
    if constexpr (endianness == std::endian::native) {
        return value;
    } else {
        return byte_swap(value);
    }
}

/// @brief Swaps the bytes of `value` if the native endianness is not big-endian.
///
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to byte swap
/// @return `value` with its bytes swapped if the native endianness is not big-endian
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T big_endian_swap(T value) noexcept {
    return endian_swap<std::endian::big>(value);
}

/// @brief Swaps the bytes of `value` if the native endianness is not little-endian.
///
/// @tparam T the type of the unsigned integral
/// @param[in] value the value to byte swap
/// @return `value` with its bytes swapped if the native endianness is not little-endian
template <std::unsigned_integral T>
[[nodiscard]] FORCE_INLINE constexpr T little_endian_swap(T value) noexcept {
    return endian_swap<std::endian::little>(value);
}

/// @brief Reverses the bits of `value`.
///
/// @param[in] value the value to reverse bits from
/// @return `value` with its bits reversed
[[nodiscard]] FORCE_INLINE constexpr uint8 reverse(uint8 value) noexcept {
#if defined(__clang__) /*|| defined(__GNUC__)*/ // builtin only available on Clang for now
    return __builtin_bitreverse8(value);
#else
    value = (value << 4u) | (value >> 4u);
    value = ((value & 0x33u) << 2u) | ((value >> 2u) & 0x33u);
    value = ((value & 0x55u) << 1u) | ((value >> 1u) & 0x55u);
    return value;
#endif
}

/// @brief Reverses the bits of `value`.
///
/// @param[in] value the value to reverse bits from
/// @return `value` with its bits reversed
[[nodiscard]] FORCE_INLINE constexpr uint16 reverse(uint16 value) noexcept {
#if defined(__clang__) /*|| defined(__GNUC__)*/ // builtin only available on Clang for now
    return __builtin_bitreverse16(value);
#else
    value = (value << 8u) | (value >> 8u);
    value = ((value & 0x0F0Fu) << 4u) | ((value >> 4u) & 0x0F0Fu);
    value = ((value & 0x3333u) << 2u) | ((value >> 2u) & 0x3333u);
    value = ((value & 0x5555u) << 1u) | ((value >> 1u) & 0x5555u);
    return value;
#endif
}

/// @brief Reverses the bits of `value`.
///
/// @param[in] value the value to reverse bits from
/// @return `value` with its bits reversed
[[nodiscard]] FORCE_INLINE constexpr uint32 reverse(uint32 value) noexcept {
#if defined(__clang__) /*|| defined(__GNUC__)*/ // builtin only available on Clang for now
    return __builtin_bitreverse32(value);
#else
    value = (value << 16u) | (value >> 16u);
    value = ((value & 0x00FF00FFu) << 8u) | ((value >> 8u) & 0x00FF00FFu);
    value = ((value & 0x0F0F0F0Fu) << 4u) | ((value >> 4u) & 0x0F0F0F0Fu);
    value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
    value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
    return value;
#endif
}

/// @brief Reverses the bits of `value`.
///
/// @param[in] value the value to reverse bits from
/// @return `value` with its bits reversed
[[nodiscard]] FORCE_INLINE constexpr uint64 reverse(uint64 value) noexcept {
#if defined(__clang__) /*|| defined(__GNUC__)*/ // builtin only available on Clang for now
    return __builtin_bitreverse64(value);
#else
    value = (value << 32ull) | (value >> 32ull);
    value = ((value & 0x0000FFFF0000FFFFull) << 16ull) | ((value >> 16ull) & 0x0000FFFF0000FFFFull);
    value = ((value & 0x00FF00FF00FF00FFull) << 8ull) | ((value >> 8ull) & 0x00FF00FF00FF00FFull);
    value = ((value & 0x0F0F0F0F0F0F0F0Full) << 4ull) | ((value >> 4ull) & 0x0F0F0F0F0F0F0F0Full);
    value = ((value & 0x3333333333333333ull) << 2ull) | ((value >> 2ull) & 0x3333333333333333ull);
    value = ((value & 0x5555555555555555ull) << 1ull) | ((value >> 1ull) & 0x5555555555555555ull);
    return value;
#endif
}

} // namespace bit

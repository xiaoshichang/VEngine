#pragma once

#include <type_traits>

namespace ve
{
    /// Opt-in trait for enum classes that should support bitwise flag operators.
    template<typename TEnum>
    struct EnableEnumFlags : std::false_type
    {
    };

    template<typename TEnum>
    concept EnumFlagsEnabled = std::is_enum_v<TEnum> && EnableEnumFlags<TEnum>::value;

    template<EnumFlagsEnabled TEnum>
    [[nodiscard]] constexpr std::underlying_type_t<TEnum> ToUnderlying(TEnum value) noexcept
    {
        return static_cast<std::underlying_type_t<TEnum>>(value);
    }

    template<EnumFlagsEnabled TEnum>
    [[nodiscard]] constexpr TEnum operator|(TEnum left, TEnum right) noexcept
    {
        return static_cast<TEnum>(ToUnderlying(left) | ToUnderlying(right));
    }

    template<EnumFlagsEnabled TEnum>
    [[nodiscard]] constexpr TEnum operator&(TEnum left, TEnum right) noexcept
    {
        return static_cast<TEnum>(ToUnderlying(left) & ToUnderlying(right));
    }

    template<EnumFlagsEnabled TEnum>
    [[nodiscard]] constexpr TEnum operator^(TEnum left, TEnum right) noexcept
    {
        return static_cast<TEnum>(ToUnderlying(left) ^ ToUnderlying(right));
    }

    template<EnumFlagsEnabled TEnum>
    [[nodiscard]] constexpr TEnum operator~(TEnum value) noexcept
    {
        return static_cast<TEnum>(~ToUnderlying(value));
    }

    template<EnumFlagsEnabled TEnum>
    constexpr TEnum& operator|=(TEnum& left, TEnum right) noexcept
    {
        left = left | right;
        return left;
    }

    template<EnumFlagsEnabled TEnum>
    constexpr TEnum& operator&=(TEnum& left, TEnum right) noexcept
    {
        left = left & right;
        return left;
    }

    template<EnumFlagsEnabled TEnum>
    constexpr TEnum& operator^=(TEnum& left, TEnum right) noexcept
    {
        left = left ^ right;
        return left;
    }
} // namespace ve

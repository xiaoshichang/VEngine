#pragma once

#include <cstdint>
#include <string_view>

#if defined(__cpp_lib_source_location) && __cpp_lib_source_location >= 201907L
#include <source_location>
#endif

namespace ve
{
#if defined(__cpp_lib_source_location) && __cpp_lib_source_location >= 201907L
    using SourceLocation = std::source_location;
#else
    struct SourceLocation
    {
        const char* fileName_ = "";
        const char* functionName_ = "";
        std::uint_least32_t line_ = 0;
        std::uint_least32_t column_ = 0;

        static constexpr SourceLocation current(const char* file = "", const char* function = "", std::uint_least32_t lineNumber = 0, std::uint_least32_t columnNumber = 0) noexcept
        {
            return SourceLocation{file, function, lineNumber, columnNumber};
        }

        [[nodiscard]] constexpr const char* file_name() const noexcept
        {
            return fileName_;
        }

        [[nodiscard]] constexpr const char* function_name() const noexcept
        {
            return functionName_;
        }

        [[nodiscard]] constexpr std::uint_least32_t line() const noexcept
        {
            return line_;
        }

        [[nodiscard]] constexpr std::uint_least32_t column() const noexcept
        {
            return column_;
        }
    };
#endif
} // namespace ve

#pragma once

namespace ve
{
    /// Base class for types that must not be copied.
    class NonCopyable
    {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;
        NonCopyable(NonCopyable&&) noexcept = default;
        NonCopyable& operator=(NonCopyable&&) noexcept = default;

    public:
        NonCopyable(const NonCopyable&) = delete;
        NonCopyable& operator=(const NonCopyable&) = delete;
    };

    /// Base class for types that must not be copied or moved.
    class NonMovable
    {
    protected:
        NonMovable() = default;
        ~NonMovable() = default;

    public:
        NonMovable(const NonMovable&) = delete;
        NonMovable& operator=(const NonMovable&) = delete;
        NonMovable(NonMovable&&) = delete;
        NonMovable& operator=(NonMovable&&) = delete;
    };
} // namespace ve

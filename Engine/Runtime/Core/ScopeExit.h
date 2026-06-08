#pragma once

#include <type_traits>
#include <utility>

namespace ve
{
    /// Runs a cleanup callable when the current scope exits unless dismissed.
    template<typename TFunction>
    class ScopeExit final
    {
    public:
        explicit ScopeExit(TFunction function) noexcept(std::is_nothrow_move_constructible_v<TFunction>)
            : function_(std::move(function))
        {
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;
        ScopeExit& operator=(ScopeExit&&) = delete;

        ScopeExit(ScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<TFunction>)
            : function_(std::move(other.function_))
            , active_(other.active_)
        {
            other.Dismiss();
        }

        ~ScopeExit() noexcept(noexcept(std::declval<TFunction&>()()))
        {
            if (active_)
            {
                function_();
            }
        }

        /// Prevents the cleanup callable from running.
        void Dismiss() noexcept
        {
            active_ = false;
        }

    private:
        TFunction function_;
        bool active_ = true;
    };

    /// Creates a ScopeExit object while preserving the callable's value category.
    template<typename TFunction>
    [[nodiscard]] ScopeExit<std::decay_t<TFunction>> MakeScopeExit(TFunction&& function)
    {
        return ScopeExit<std::decay_t<TFunction>>(std::forward<TFunction>(function));
    }
} // namespace ve

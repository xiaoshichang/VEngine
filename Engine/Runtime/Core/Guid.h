#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"

#include <boost/uuid/uuid.hpp>
#include <functional>
#include <string>

namespace ve
{
    class Guid
    {
    public:
        Guid() = default;
        explicit Guid(boost::uuids::uuid value);

        [[nodiscard]] static Guid Create();
        [[nodiscard]] static Result<Guid> Parse(const std::string& text);

        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const boost::uuids::uuid& GetValue() const noexcept;

        [[nodiscard]] friend bool operator==(const Guid& left, const Guid& right) noexcept = default;

    private:
        boost::uuids::uuid value_{};
    };
} // namespace ve

namespace std
{
    template<>
    struct hash<ve::Guid>
    {
        [[nodiscard]] size_t operator()(const ve::Guid& guid) const noexcept;
    };
} // namespace std

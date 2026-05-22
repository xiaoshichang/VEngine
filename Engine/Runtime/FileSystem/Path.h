#pragma once

#include <string>
#include <string_view>

namespace ve
{
class Path
{
public:
    Path() = default;
    Path(const char* value);
    explicit Path(std::string value);
    explicit Path(std::string_view value);

    [[nodiscard]] const std::string& GetString() const noexcept;
    [[nodiscard]] const char* CStr() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] bool IsAbsolute() const noexcept;
    [[nodiscard]] Path GetParentPath() const;
    [[nodiscard]] std::string GetFilename() const;
    [[nodiscard]] std::string GetExtension() const;

    [[nodiscard]] Path operator/(const Path& other) const;
    [[nodiscard]] Path operator/(const char* other) const;
    [[nodiscard]] Path operator/(std::string_view other) const;

    [[nodiscard]] friend bool operator==(const Path& left, const Path& right) noexcept = default;

private:
    struct PrivateTag
    {
    };

    Path(std::string value, PrivateTag);

    friend Path NormalizePath(std::string_view path);

    std::string value_;
};

[[nodiscard]] Path NormalizePath(std::string_view path);
[[nodiscard]] Path JoinPath(const Path& left, const Path& right);
[[nodiscard]] Path JoinPath(const Path& left, const char* right);
[[nodiscard]] Path JoinPath(const Path& left, std::string_view right);
}

#include "Engine/Runtime/FileSystem/Path.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ve
{
    namespace
    {
        struct ParsedRoot
        {
            std::string root;
            size_t offset = 0;
            bool absolute = false;
        };

        [[nodiscard]] bool IsAsciiAlpha(char value) noexcept
        {
            return std::isalpha(static_cast<unsigned char>(value)) != 0;
        }

        [[nodiscard]] std::string ReplaceSlashes(std::string_view path)
        {
            std::string result(path);
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        [[nodiscard]] ParsedRoot ParseRoot(const std::string& path)
        {
            ParsedRoot parsed;

            if (path.size() >= 3 && IsAsciiAlpha(path[0]) && path[1] == ':' && path[2] == '/')
            {
                parsed.root.assign(path.data(), 3);
                parsed.offset = 3;
                parsed.absolute = true;
                return parsed;
            }

            if (path.starts_with("//") && path.size() > 2 && path[2] != '/')
            {
                const size_t serverStart = 2;
                const size_t serverEnd = path.find('/', serverStart);

                if (serverEnd == std::string::npos)
                {
                    parsed.root = path;
                    parsed.offset = path.size();
                }
                else
                {
                    size_t shareStart = serverEnd + 1;

                    while (shareStart < path.size() && path[shareStart] == '/')
                    {
                        ++shareStart;
                    }

                    if (shareStart == path.size())
                    {
                        parsed.root = path.substr(0, serverEnd);
                        parsed.offset = path.size();
                        parsed.absolute = true;
                        return parsed;
                    }

                    const size_t shareEnd = path.find('/', shareStart);
                    const std::string server = path.substr(serverStart, serverEnd - serverStart);

                    if (shareEnd == std::string::npos)
                    {
                        parsed.root = "//" + server + "/" + path.substr(shareStart);
                        parsed.offset = path.size();
                    }
                    else
                    {
                        parsed.root = "//" + server + "/" + path.substr(shareStart, shareEnd - shareStart);
                        parsed.offset = shareEnd + 1;
                    }
                }

                parsed.absolute = true;
                return parsed;
            }

            if (path.starts_with("/"))
            {
                parsed.root = "/";
                parsed.offset = 1;
                parsed.absolute = true;
                return parsed;
            }

            return parsed;
        }

        [[nodiscard]] std::vector<std::string> SplitSegments(const std::string& path, size_t offset)
        {
            std::vector<std::string> segments;
            size_t segmentStart = offset;

            for (size_t index = offset; index <= path.size(); ++index)
            {
                if (index == path.size() || path[index] == '/')
                {
                    if (index > segmentStart)
                    {
                        segments.emplace_back(path.substr(segmentStart, index - segmentStart));
                    }

                    segmentStart = index + 1;
                }
            }

            return segments;
        }

        [[nodiscard]] std::string BuildPath(const ParsedRoot& root, const std::vector<std::string>& segments)
        {
            std::string result = root.root;

            for (const std::string& segment : segments)
            {
                if (!result.empty() && !result.ends_with('/'))
                {
                    result.push_back('/');
                }

                result += segment;
            }

            return result;
        }

        [[nodiscard]] bool CanPopSegment(const std::vector<std::string>& segments)
        {
            if (segments.empty())
            {
                return false;
            }

            if (segments.back() == "..")
            {
                return false;
            }

            return true;
        }
    } // namespace

    Path::Path(const char* value)
        : Path(value != nullptr ? std::string_view(value) : std::string_view())
    {
    }

    Path::Path(std::string value)
        : Path(std::string_view(value))
    {
    }

    Path::Path(std::string_view value)
        : value_(NormalizePath(value).GetString())
    {
    }

    Path::Path(std::string value, PrivateTag)
        : value_(std::move(value))
    {
    }

    const std::string& Path::GetString() const noexcept
    {
        return value_;
    }

    const char* Path::CStr() const noexcept
    {
        return value_.c_str();
    }

    bool Path::IsEmpty() const noexcept
    {
        return value_.empty();
    }

    bool Path::IsAbsolute() const noexcept
    {
        if (value_.starts_with("/"))
        {
            return true;
        }

        return value_.size() >= 3 && IsAsciiAlpha(value_[0]) && value_[1] == ':' && value_[2] == '/';
    }

    Path Path::GetParentPath() const
    {
        if (value_.empty())
        {
            return Path();
        }

        const ParsedRoot root = ParseRoot(value_);

        if (value_ == root.root)
        {
            return Path(value_);
        }

        const size_t slash = value_.find_last_of('/');

        if (slash == std::string::npos)
        {
            return Path();
        }

        if (slash < root.root.size())
        {
            return Path(root.root);
        }

        if (slash == 0)
        {
            return Path("/");
        }

        return Path(value_.substr(0, slash));
    }

    std::string Path::GetFilename() const
    {
        if (value_.empty())
        {
            return {};
        }

        const ParsedRoot root = ParseRoot(value_);

        if (value_ == root.root)
        {
            return {};
        }

        const size_t slash = value_.find_last_of('/');

        if (slash == std::string::npos)
        {
            return value_;
        }

        return value_.substr(slash + 1);
    }

    std::string Path::GetExtension() const
    {
        const std::string filename = GetFilename();
        const size_t dot = filename.find_last_of('.');

        if (dot == std::string::npos || dot == 0)
        {
            return {};
        }

        return filename.substr(dot);
    }

    Path Path::operator/(const Path& other) const
    {
        return JoinPath(*this, other);
    }

    Path Path::operator/(const char* other) const
    {
        return JoinPath(*this, other);
    }

    Path Path::operator/(std::string_view other) const
    {
        return JoinPath(*this, other);
    }

    Path NormalizePath(std::string_view path)
    {
        const std::string slashPath = ReplaceSlashes(path);
        const ParsedRoot root = ParseRoot(slashPath);
        const std::vector<std::string> sourceSegments = SplitSegments(slashPath, root.offset);
        std::vector<std::string> normalizedSegments;

        for (const std::string& segment : sourceSegments)
        {
            if (segment.empty() || segment == ".")
            {
                continue;
            }

            if (segment == "..")
            {
                if (CanPopSegment(normalizedSegments))
                {
                    normalizedSegments.pop_back();
                }
                else if (!root.absolute)
                {
                    normalizedSegments.push_back(segment);
                }

                continue;
            }

            normalizedSegments.push_back(segment);
        }

        return Path(BuildPath(root, normalizedSegments), Path::PrivateTag{});
    }

    Path JoinPath(const Path& left, const Path& right)
    {
        if (right.IsEmpty())
        {
            return left;
        }

        if (right.IsAbsolute() || left.IsEmpty())
        {
            return right;
        }

        return NormalizePath(left.GetString() + "/" + right.GetString());
    }

    Path JoinPath(const Path& left, const char* right)
    {
        return JoinPath(left, Path(right));
    }

    Path JoinPath(const Path& left, std::string_view right)
    {
        return JoinPath(left, Path(right));
    }
} // namespace ve

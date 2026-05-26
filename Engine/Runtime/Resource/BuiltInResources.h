#pragma once

#include <string_view>

namespace ve
{
    namespace BuiltInResources
    {
        inline constexpr std::string_view Scheme = "builtin:";

        inline constexpr std::string_view FallbackCubeMeshUri = "builtin:mesh/cube";
        inline constexpr std::string_view DefaultMaterialUri = "builtin:material/default";

        inline constexpr std::string_view FallbackCubeMeshName = "BuiltIn.Cube";
        inline constexpr std::string_view DefaultMaterialName = "BuiltIn.DefaultMaterial";

        [[nodiscard]] bool IsBuiltInUri(std::string_view uri) noexcept;
    } // namespace BuiltInResources
} // namespace ve

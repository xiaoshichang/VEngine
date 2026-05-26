#include "Engine/Runtime/Resource/BuiltInResources.h"

namespace ve
{
    namespace BuiltInResources
    {
        bool IsBuiltInUri(std::string_view uri) noexcept
        {
            return uri.starts_with(Scheme);
        }
    } // namespace BuiltInResources
} // namespace ve

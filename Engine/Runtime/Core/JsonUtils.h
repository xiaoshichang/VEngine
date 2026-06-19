#pragma once

#include "Engine/Runtime/Core/Result.h"

#include <boost/json.hpp>
#include <string>
#include <string_view>

namespace ve::JsonUtils
{
    [[nodiscard]] std::string SerializePretty(const boost::json::value& value, int indentSpaces = 4);
    [[nodiscard]] Result<boost::json::value> Parse(std::string_view text);
} // namespace ve::JsonUtils

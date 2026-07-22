#include "Engine/Runtime/Core/JsonUtils.h"

#include "Engine/Runtime/Core/Types.h"

#include <charconv>
#include <cmath>

namespace ve::JsonUtils
{
    namespace
    {
        template<typename Number>
        [[nodiscard]] bool AppendReadableFiniteNumber(std::string& output, Number value)
        {
            constexpr double MinimumFixedMagnitude = 1.0e-6;
            constexpr double MaximumFixedMagnitude = 1.0e16;
            const double magnitude = std::abs(static_cast<double>(value));
            const std::chars_format format = magnitude >= MinimumFixedMagnitude && magnitude < MaximumFixedMagnitude ? std::chars_format::fixed
                                                                                                                      : std::chars_format::general;
            char buffer[128] = {};
            const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value, format);
            if (result.ec != std::errc())
            {
                return false;
            }

            output.append(buffer, result.ptr);
            return true;
        }

        void SerializeScalar(std::string& output, const boost::json::value& value)
        {
            if (!value.is_double())
            {
                output += boost::json::serialize(value);
                return;
            }

            const double number = value.as_double();
            if (!std::isfinite(number))
            {
                output += boost::json::serialize(value);
                return;
            }
            if (number == 0.0)
            {
                output += "0";
                return;
            }

            if (AppendReadableFiniteNumber(output, number))
            {
                return;
            }

            output += boost::json::serialize(value);
        }

        void WriteIndent(std::string& output, int depth, int indentSpaces)
        {
            output.append(static_cast<size_t>(depth * indentSpaces), ' ');
        }

        [[nodiscard]] bool ShouldSerializeArrayInline(const boost::json::array& array) noexcept
        {
            for (const boost::json::value& item : array)
            {
                if (item.is_object() || item.is_array())
                {
                    return false;
                }
            }

            return true;
        }

        void SerializeInlineArray(std::string& output, const boost::json::array& array)
        {
            output += "[";
            for (size_t index = 0; index < array.size(); ++index)
            {
                if (index > 0)
                {
                    output += ", ";
                }

                SerializeScalar(output, array[index]);
            }
            output += "]";
        }

        void SerializePrettyImpl(std::string& output, const boost::json::value& value, int depth, int indentSpaces)
        {
            switch (value.kind())
            {
            case boost::json::kind::object:
            {
                const boost::json::object& object = value.as_object();
                if (object.empty())
                {
                    output += "{}";
                    return;
                }

                output += "{\n";
                size_t index = 0;
                for (const auto& item : object)
                {
                    WriteIndent(output, depth + 1, indentSpaces);
                    output += boost::json::serialize(boost::json::value(item.key()));
                    output += ": ";
                    SerializePrettyImpl(output, item.value(), depth + 1, indentSpaces);
                    if (++index < object.size())
                    {
                        output += ",";
                    }
                    output += "\n";
                }
                WriteIndent(output, depth, indentSpaces);
                output += "}";
                break;
            }
            case boost::json::kind::array:
            {
                const boost::json::array& array = value.as_array();
                if (array.empty())
                {
                    output += "[]";
                    return;
                }

                if (ShouldSerializeArrayInline(array))
                {
                    SerializeInlineArray(output, array);
                    return;
                }

                output += "[\n";
                for (size_t index = 0; index < array.size(); ++index)
                {
                    WriteIndent(output, depth + 1, indentSpaces);
                    SerializePrettyImpl(output, array[index], depth + 1, indentSpaces);
                    if (index + 1 < array.size())
                    {
                        output += ",";
                    }
                    output += "\n";
                }
                WriteIndent(output, depth, indentSpaces);
                output += "]";
                break;
            }
            default:
                SerializeScalar(output, value);
                break;
            }
        }
    } // namespace

    boost::json::value MakeFloat(Float32 value)
    {
        if (!std::isfinite(value))
        {
            return static_cast<double>(value);
        }
        if (value == 0.0f)
        {
            return 0.0;
        }

        char buffer[64] = {};
        const std::to_chars_result formatResult = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (formatResult.ec != std::errc())
        {
            return static_cast<double>(value);
        }

        double canonicalValue = 0.0;
        const std::from_chars_result parseResult = std::from_chars(buffer, formatResult.ptr, canonicalValue);
        if (parseResult.ec != std::errc() || parseResult.ptr != formatResult.ptr)
        {
            return static_cast<double>(value);
        }
        return canonicalValue;
    }

    std::string SerializePretty(const boost::json::value& value, int indentSpaces)
    {
        if (indentSpaces <= 0)
        {
            indentSpaces = 4;
        }

        std::string output;
        SerializePrettyImpl(output, value, 0, indentSpaces);
        output += "\n";
        return output;
    }

    Result<boost::json::value> Parse(std::string_view text)
    {
        boost::system::error_code errorCode;
        boost::json::value value = boost::json::parse(text, errorCode);
        if (errorCode)
        {
            return Result<boost::json::value>::Failure(Error(ErrorCode::InvalidArgument, "JSON parse failed: " + errorCode.message()));
        }

        return Result<boost::json::value>::Success(std::move(value));
    }
} // namespace ve::JsonUtils

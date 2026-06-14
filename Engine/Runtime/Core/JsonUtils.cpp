#include "Engine/Runtime/Core/JsonUtils.h"

namespace ve::JsonUtils
{
    namespace
    {
        void WriteIndent(std::string& output, int depth, int indentSpaces)
        {
            output.append(static_cast<size_t>(depth * indentSpaces), ' ');
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
                output += boost::json::serialize(value);
                break;
            }
        }
    } // namespace

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
            return Result<boost::json::value>::Failure(
                Error(ErrorCode::InvalidArgument, "JSON parse failed: " + errorCode.message()));
        }

        return Result<boost::json::value>::Success(std::move(value));
    }
} // namespace ve::JsonUtils

#include "Engine/Runtime/Core/JsonUtils.h"

#include <boost/json.hpp>
#include <iostream>
#include <string>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestReadableNumbers()
    {
        boost::json::object object;
        object["zero"] = ve::JsonUtils::MakeFloat(0.0f);
        object["negativeZero"] = ve::JsonUtils::MakeFloat(-0.0f);
        object["position"] = boost::json::array{
            ve::JsonUtils::MakeFloat(0.0f), ve::JsonUtils::MakeFloat(8.449999809265137f), ve::JsonUtils::MakeFloat(0.0f)};
        object["rotation"] = boost::json::array{ve::JsonUtils::MakeFloat(0.7071068286895752f),
                                                 ve::JsonUtils::MakeFloat(0.0f),
                                                 ve::JsonUtils::MakeFloat(0.0f),
                                                 ve::JsonUtils::MakeFloat(0.7071068286895752f)};
        object["smallValue"] = ve::JsonUtils::MakeFloat(0.001f);
        object["preciseDouble"] = 1.2345678901234567;
        object["floatExactDouble"] = static_cast<double>(8.449999809265137f);

        const std::string expected = R"({
    "zero": 0,
    "negativeZero": 0,
    "position": [0, 8.45, 0],
    "rotation": [0.7071068, 0, 0, 0.7071068],
    "smallValue": 0.001,
    "preciseDouble": 1.2345678901234567,
    "floatExactDouble": 8.449999809265137
}
)";
        return Expect(ve::JsonUtils::SerializePretty(object) == expected,
                      "Pretty JSON should use readable shortest lossless numbers without ordinary scientific notation");
    }

    bool TestReadableNumbersAreStable()
    {
        boost::json::object object;
        object["float"] = ve::JsonUtils::MakeFloat(8.449999809265137f);
        object["double"] = 1.2345678901234567;
        const std::string first = ve::JsonUtils::SerializePretty(object);
        const ve::Result<boost::json::value> parsed = ve::JsonUtils::Parse(first);
        if (!Expect(static_cast<bool>(parsed), "Readable pretty JSON should parse successfully"))
        {
            return false;
        }

        return Expect(ve::JsonUtils::SerializePretty(parsed.GetValue()) == first,
                      "Parsing and serializing readable numbers should produce stable output");
    }
} // namespace

int main()
{
    if (TestReadableNumbers() && TestReadableNumbersAreStable())
    {
        std::cout << "VEngineJsonUtilsTests passed" << '\n';
        return 0;
    }

    return 1;
}

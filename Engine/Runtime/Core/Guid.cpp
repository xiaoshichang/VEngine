#include "Engine/Runtime/Core/Guid.h"

#include <boost/functional/hash.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace ve
{
    Guid::Guid(boost::uuids::uuid value)
        : value_(value)
    {
    }

    Guid Guid::Create()
    {
        static boost::uuids::random_generator generator;
        return Guid(generator());
    }

    Result<Guid> Guid::Parse(const std::string& text)
    {
        try
        {
            boost::uuids::string_generator generator;
            return Result<Guid>::Success(Guid(generator(text)));
        }
        catch (const std::exception&)
        {
            return Result<Guid>::Failure(Error(ErrorCode::InvalidArgument, "Invalid GUID string."));
        }
    }

    bool Guid::IsEmpty() const noexcept
    {
        return value_.is_nil();
    }

    std::string Guid::ToString() const
    {
        return boost::uuids::to_string(value_);
    }

    const boost::uuids::uuid& Guid::GetValue() const noexcept
    {
        return value_;
    }
} // namespace ve

namespace std
{
    size_t hash<ve::Guid>::operator()(const ve::Guid& guid) const noexcept
    {
        return boost::hash<boost::uuids::uuid>()(guid.GetValue());
    }
} // namespace std

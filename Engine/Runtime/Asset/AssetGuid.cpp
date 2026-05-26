#include "Engine/Runtime/Asset/AssetGuid.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <exception>

namespace ve
{
    AssetGuid::AssetGuid(const boost::uuids::uuid& uuid) noexcept
        : uuid_(uuid)
    {
    }

    AssetGuid AssetGuid::Generate()
    {
        return AssetGuid(boost::uuids::random_generator()());
    }

    Result<AssetGuid> AssetGuid::Parse(std::string_view text)
    {
        if (text.empty())
        {
            return Result<AssetGuid>::Failure(Error(ErrorCode::InvalidArgument, "AssetGuid text is empty."));
        }

        try
        {
            const boost::uuids::uuid uuid = boost::uuids::string_generator()(std::string(text));
            return Result<AssetGuid>::Success(AssetGuid(uuid));
        }
        catch (const std::exception& exception)
        {
            return Result<AssetGuid>::Failure(
                Error(ErrorCode::InvalidArgument, std::string("Invalid AssetGuid: ") + exception.what()));
        }
    }

    bool AssetGuid::IsValid() const noexcept
    {
        return !uuid_.is_nil();
    }

    std::string AssetGuid::ToString() const
    {
        return boost::uuids::to_string(uuid_);
    }
} // namespace ve

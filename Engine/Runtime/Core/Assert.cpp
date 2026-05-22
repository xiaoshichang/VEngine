#include "Engine/Runtime/Core/Assert.h"

#include <atomic>
#include <cstdio>

namespace
{
void DefaultAssertionHandler(const ve::AssertionInfo& info) noexcept
{
    std::fprintf(
        stderr,
        "VEngine assertion failed\n"
        "  Expression: %s\n"
        "  Message: %s\n"
        "  File: %s:%d\n"
        "  Function: %s\n",
        info.expression != nullptr ? info.expression : "<unknown>",
        info.message != nullptr ? info.message : "<none>",
        info.file != nullptr ? info.file : "<unknown>",
        info.line,
        info.function != nullptr ? info.function : "<unknown>");
}

std::atomic<ve::AssertionHandler> gAssertionHandler{DefaultAssertionHandler};
}

namespace ve
{
void SetAssertionHandler(AssertionHandler handler) noexcept
{
    gAssertionHandler.store(handler != nullptr ? handler : DefaultAssertionHandler, std::memory_order_release);
}

AssertionHandler GetAssertionHandler() noexcept
{
    AssertionHandler handler = gAssertionHandler.load(std::memory_order_acquire);
    return handler != nullptr ? handler : DefaultAssertionHandler;
}

void ReportAssertionFailure(const AssertionInfo& info) noexcept
{
    GetAssertionHandler()(info);
}
}

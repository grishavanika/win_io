#pragma once
#include <cstdint>

namespace wi
{
    // You will have compile time error if mismatch
    // with real Win API types from Windows.h will
    // be detected (size and alignment validated)
    using WinHANDLE = void*;
    using WinSOCKET = void*;
    using WinDWORD = std::uint32_t;
    using WinULONG_PTR = std::uintptr_t;
        
    struct WinOVERLAPPED
    {
        WinULONG_PTR Internal;
        WinULONG_PTR InternalHigh;
        union
        {
            struct
            {
                WinDWORD Offset;
                WinDWORD OffsetHigh;
            } _;
            void* Pointer;
        };
        WinHANDLE hEvent;
    };
} // namespace wi

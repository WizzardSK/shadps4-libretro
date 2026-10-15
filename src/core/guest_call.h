// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <type_traits>
#include "common/types.h"

#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
#include "core/guest_cpu/guest_callback.h"
#endif

namespace Core {

// Calls a function of the game's (a callback it registered, an entry point): on the aarch64
// core with the FEXCore backend through FEX, as the game's code is x86-64; elsewhere directly.
// Arguments are integers, enums and pointers.
template <typename R, typename... Params, typename... Args>
R CallGuest(R(PS4_SYSV_ABI* function)(Params...), Args... args) {
#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
    const auto* address = reinterpret_cast<const void*>(function);
    if (GuestCpu::IsGuestFunctionAddress(address)) {
        const u64 result =
            GuestCpu::RunGuestFunctionOrAbort(address, "guest call", static_cast<Params>(args)...);
        if constexpr (std::is_void_v<R>) {
            return;
        } else if constexpr (std::is_pointer_v<R>) {
            return reinterpret_cast<R>(result);
        } else {
            return static_cast<R>(result);
        }
    }
#endif
    return function(args...);
}

} // namespace Core

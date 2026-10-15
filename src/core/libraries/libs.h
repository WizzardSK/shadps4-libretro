// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/loader/elf.h"
#include "core/loader/symbols_resolver.h"
#include "core/tls.h"
#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
#include "core/guest_cpu/hle_call_adapter.h"
#endif

void LinkSymbolImpl(Core::Loader::SymbolsResolver* sym, char const* nid, char const* lib,
                    u16 libversion, char const* mod, u64 symbol, Core::Loader::SymbolType sym_type);

#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
void LinkFunctionImpl(Core::Loader::SymbolsResolver* sym, char const* nid, char const* lib,
                      u16 libversion, char const* mod,
                      std::shared_ptr<Core::GuestCpu::HleCallAdapter> adapter);

// FEX reaches an HLE function through an x86 syscall veneer, never an ARM host address
#define LIB_FUNCTION(nid, lib, libversion, mod, function)                                          \
    LinkFunctionImpl(sym, nid, lib, libversion, mod, Core::GuestCpu::MakeHleCallAdapter(function))
#else
#define LIB_FUNCTION(nid, lib, libversion, mod, function)                                          \
    LinkSymbolImpl(sym, nid, lib, libversion, mod, reinterpret_cast<u64>(HOST_CALL(function)),     \
                   Core::Loader::SymbolType::Function)
#endif

#define LIB_OBJ(nid, lib, libversion, mod, obj)                                                    \
    LinkSymbolImpl(sym, nid, lib, libversion, mod, reinterpret_cast<u64>(obj),                     \
                   Core::Loader::SymbolType::Object)

namespace Libraries {

void InitHLELibs(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries

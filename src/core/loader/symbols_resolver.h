// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "common/assert.h"
#include "common/types.h"

#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
#include <memory>
namespace Core::GuestCpu {
class HleCallAdapter;
class HleCallRegistry;
} // namespace Core::GuestCpu
#endif

namespace Core::Loader {

enum class SymbolType {
    Unknown,
    Function,
    Object,
    Tls,
    NoType,
};

struct SymbolResolver {
    std::string name;
    std::string nidName;
    std::string library;
    u16 library_version;
    std::string module;
    SymbolType type;
    bool operator==(SymbolResolver const& o) const;
};

struct SymbolRecord {
    SymbolResolver symbol;
    u64 virtual_address;
#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
    // An HLE function, reached by the guest through an x86 syscall veneer (no host address)
    std::shared_ptr<GuestCpu::HleCallAdapter> hle_adapter;
    bool hle_fallback{};
#endif
};

class SymbolsResolver {
public:
    SymbolsResolver();
    virtual ~SymbolsResolver();

    void AddSymbol(const SymbolResolver& s, u64 virtual_addr);
#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
    void AddFunction(const SymbolResolver& s, std::shared_ptr<GuestCpu::HleCallAdapter> adapter);
    const std::shared_ptr<GuestCpu::HleCallAdapter>& AddUnsupportedFunction(const SymbolResolver& s);
    std::shared_ptr<GuestCpu::HleCallAdapter> FindFunction(u64 operation) const;
    GuestCpu::HleCallRegistry& GetHleCallRegistry();
#endif
    const SymbolRecord* FindSymbol(const SymbolResolver& s) const;

    void DebugDump(const std::filesystem::path& file_name);

    std::span<const SymbolRecord> GetSymbols() const {
        return m_symbols;
    }

    size_t GetSize() const noexcept {
        return m_symbols.size();
    }

    static std::string_view SymbolTypeToS(SymbolType sym_type);

private:
    std::vector<SymbolRecord> m_symbols;
#ifdef SHADPS4_ENABLE_FEX_GUEST_CPU
    std::unique_ptr<GuestCpu::HleCallRegistry> hle_registry;
#endif
};

} // namespace Core::Loader

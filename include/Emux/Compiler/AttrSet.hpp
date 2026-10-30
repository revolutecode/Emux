#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Emux
{

// ---------- Atributos ----------
enum class AttrId : uint8_t {
    Const, 
    Volatile,
    Everen,
    NoWarn,
    NoExcept,
    Restrict,
    Unrestrict,
    Inline,
    AlwaysInline,
    Pure,
    Import,
    Export,
    Stdcall,
    Cdecl,
    Fastcall,
    Count
};

struct AttrSet {
    std::bitset<size_t(AttrId::Count)> flags;
    // Valor opcional de cada atributo, ex: [[import("kernel32")]].
    std::array<std::string, size_t(AttrId::Count)> values;

    void Set(AttrId id) { flags.set(size_t(id)); }
    void Set(AttrId id, std::string value) {
        flags.set(size_t(id));
        values[size_t(id)] = std::move(value);
    }
    bool Has(AttrId id) const { return flags.test(size_t(id)); }
    std::string_view Value(AttrId id) const { return values[size_t(id)]; }
    bool Empty() const { return flags.none(); }
    void Merge(const AttrSet& o) {
        flags |= o.flags;
        for (size_t i = 0; i < values.size(); ++i) {
            if (!o.values[i].empty()) values[i] = o.values[i];
        }
    }
    void Clear() {
        flags.reset();
        for (std::string& value : values) value.clear();
    }
};

static const std::unordered_map<std::string, AttrId> kAttrTable = {
    {"const", AttrId::Const},
    {"volatile", AttrId::Volatile},
    {"everen", AttrId::Everen},
    {"nowarn", AttrId::NoWarn},
    {"noexcept", AttrId::NoExcept},
    {"restrict", AttrId::Restrict},
    {"unrestrict", AttrId::Unrestrict},
    {"inline", AttrId::Inline},
    {"alwaysinline", AttrId::AlwaysInline},
    {"pure", AttrId::Pure},
    {"import", AttrId::Import},
    {"export", AttrId::Export},
    {"stdcall", AttrId::Stdcall},
    {"cdecl", AttrId::Cdecl},
    {"fastcall", AttrId::Fastcall},
};

}

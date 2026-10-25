#pragma once
#include <bitset>
#include <cstdint>
#include <string>
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
    Count
};

struct AttrSet {
    std::bitset<size_t(AttrId::Count)> flags;

    void Set(AttrId id) { flags.set(size_t(id)); }
    bool Has(AttrId id) const { return flags.test(size_t(id)); }
    bool Empty() const { return flags.none(); }
    void Merge(const AttrSet& o) { flags |= o.flags; }
    void Clear() { flags.reset(); }
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
};

}

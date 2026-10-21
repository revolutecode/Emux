#pragma once
#include <bitset>
#include <cstdint>
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
    Count
};

struct AttrSet {
    // Um bit por AttrId; o tamanho vem do enum, então não há limite fixo de atributos.
    std::bitset<size_t(AttrId::Count)> flags;

    void Set(AttrId id) { flags.set(size_t(id)); }
    bool Has(AttrId id) const { return flags.test(size_t(id)); }
    bool Empty() const { return flags.none(); }
    void Merge(const AttrSet& o) { flags |= o.flags; }
};

// Registro: único lugar que conhece os nomes dos atributos (chaves em minúsculas).
static const std::unordered_map<std::string, AttrId> kAttrTable = {
    {"const", AttrId::Const},
    {"volatile", AttrId::Volatile},
    {"everen", AttrId::Everen},
    {"nowarn", AttrId::NoWarn},
    {"noexcept", AttrId::NoExcept},
    {"restrict", AttrId::Restrict},
    {"unrestrict", AttrId::Unrestrict},
};

}

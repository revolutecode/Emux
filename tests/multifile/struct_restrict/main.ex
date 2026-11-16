[Main]

global Start() i32 {
    local p : Types::Pair = { 10, 20 }
    local t : Types::Twin = { 3, 4 }
    // SumStrict: identidade nominal exata (30). SumLoose: Twin e estruturalmente
    // equivalente a Pair via [[unrestrict]], aceite por omissao (7).
    return Types::SumStrict(p) + Types::SumLoose(t)
}
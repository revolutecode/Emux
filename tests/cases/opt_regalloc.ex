[Main]

// Cadeia de temporarios de vida curta: cada resultado e consumido pela
// operacao seguinte. Em -O0 cada temporario vai a pilha e e recarregado;
// em -O2 o forwarding store->load mantem-no no registo.
global Start() i32 {
    local a : i32 = 3
    local b : i32 = 5
    local c : i32 = a + b
    local d : i32 = c * 2
    local e : i32 = d - a
    local f : i32 = e + b
    local g : i32 = f + c
    return g
}
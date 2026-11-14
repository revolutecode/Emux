[Main]

# Blocos no frame passam pelos mesmos slots que os parametros: o cmpmem tem de
# ler enderecos de [rbp-of], nao de simbolos estaticos.
global Start() i32 {
    local a : B4 = "AB"
    local b : B4 = "AB"
    local c : B3 = "AC"

    local r : i32 = 0

    if (a == b) { r = r + 1 }
    if (a < c) { r = r + 2 }
    if (a > c) { r = r + 4 }

    return r
}

[Main]

# O conteudo manda antes do desempate: aqui os tamanhos differentes dariam
# 'p > q', mas o terceiro byte ('B' contra 'C') decide primeiro.
var p : B5 = "ABB"
var q : B4 = "ABC"
var x : B5 = "ABZ"
var y : B4 = "ABC"

func Start() i32 {
    local r : i32 = 0

    if (p > q) { r = r + 1 }
    if (p < q) { r = r + 2 }
    if (p == q) { r = r + 4 }
    if (x > y) { r = r + 8 }
    if (x < y) { r = r + 16 }

    # 2 + 8
    return r
}

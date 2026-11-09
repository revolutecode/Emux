[Main]

# Mesma capacidade: o tamanho nao decide nada, sao os bytes que falam.
var a : B4 = "AB"
var b : B4 = "AB"
var c : B4 = "AC"

func Start() i32 {
    local r : i32 = 0

    if (a == b) { r = r + 1 }
    if (a != c) { r = r + 2 }
    if (a == c) { r = r + 4 }
    if (a < c) { r = r + 8 }
    if (a > c) { r = r + 16 }
    if (a <= b) { r = r + 32 }

    # 1 + 2 + 8 + 32
    return r
}

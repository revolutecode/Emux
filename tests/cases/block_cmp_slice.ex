[Main]

# Slices sao blocos com a capacidade restante: 'a' tem 8 bytes, 'b + 1' tem 7,
# e o conteudo dos 7 comuns coincide byte a byte. Decide o desempate de 8 - 7.
global a : B8 = "XYZ"
global b : B8 = "QXYZ"

global Start() i32 {
    local r : i32 = 0

    if (a == (b + 1)) { r = r + 1 }
    if (a > (b + 1)) { r = r + 2 }
    if ((b + 1) < a) { r = r + 4 }
    if ((a + 1) != (b + 2)) { r = r + 8 }
    if ((a + 2) > (b + 3)) { r = r + 16 }

    # 2 + 4 + 8 + 16
    return r
}

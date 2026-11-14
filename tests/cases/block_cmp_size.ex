[Main]

# Capacidades diferentes: '==' e '!=' dobram-se em compilacao porque o
# conteudo nao pode empatar. Nas restantes, os 3 bytes comuns empatam e e
# o desempate estatico (sinal de 5 - 4) que decide.
global big : B5 = "ABC"
global small : B4 = "ABC"

global Start() i32 {
    local r : i32 = 0

    if (big == small) { r = r + 1 }
    if (big != small) { r = r + 2 }
    if (big > small) { r = r + 4 }
    if (big < small) { r = r + 8 }
    if (big >= small) { r = r + 16 }
    if (small <= big) { r = r + 32 }

    # 2 + 4 + 16 + 32
    return r
}

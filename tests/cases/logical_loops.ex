[Main]

func Start() i32 {
    local i : i32 = 0
    local n : i32 = 0
    local guard : i32 = 1

    # O curto-circuito tambem serve de guarda do loop: quando guard passa a 0
    # o lado direito nao e mais avaliado e o ciclo para.
    while (guard != 0 && i < 10) {
        i = i + 1

        if (i == 4) {
            guard = 0
        }

        n = n + i
    }

    local j : i32 = 0
    local total : i32 = 0

    for (j = 0; j < 8; j = j + 1) {
        if (j % 2 != 0 || j == 8) {
            total = total + j
        }
    }

    # n = 1+2+3+4 = 10, total = 1+3+5+7 = 16
    return n + total
}

[Main]

global i : i32

# Depois de um 'break' ou de um 'continue' o bloco basico fica fechado. O codigo
# que se seguir nao pode cair antes do salto: as duas somas de penalizacao abaixo
# tem de ser mortas, senao o resultado deixa de ser 3.
global Start() i32 {
    local penalty : i32 = 0

    i = 0
    while (i < 10) {
        i = i + 1
        if (i == 3) {
            continue
            penalty = penalty + 1000
        }
        if (i == 5) {
            break
            penalty = penalty + 2000
        }
        penalty = penalty + 1
    }

    return penalty
}

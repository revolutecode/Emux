[Main]

global calls : i32 = 0

global Touch() i32 {
    calls = calls + 1
    return 1
}

global Start() i32 {
    local zero : i32 = 0
    local one : i32 = 1

    # '&&' com esquerdo falso: o direito nao corre e o ramo nao e tomado.
    if (zero != 0 && Touch() != 0) {
        calls = calls + 100
    }

    # '||' com esquerdo verdadeiro: o direito nao corre. Aqui o ramo E tomado,
    # porque a condicao e mesmo verdadeira — por isso marca com +10.
    if (one != 0 || Touch() != 0) {
        calls = calls + 10
    }

    # Nenhum dos dois lados decide: Touch corre nas duas vezes.
    if (one != 0 && Touch() != 0) {
    }

    if (zero != 0 || Touch() != 0) {
    }

    # Em contexto de valor tambem se curto-circuita: Touch nao corre e os
    # resultados sao os esperados (a = 0, b = 1).
    local a : i32 = zero != 0 && Touch() != 0
    local b : i32 = one != 0 || Touch() != 0

    if (a == 0 && b == 1) {
    } else {
        calls = calls + 1000
    }

    return calls
}

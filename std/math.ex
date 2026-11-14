# STD - Utilidades Matematicas e Aritmetica Inteira para Emux

@ifndef EMUX_STD_MATH
@define EMUX_STD_MATH

[std]

# Retorna o valor absoluto de x
func abs(i64 x) i64 {
    if (x < 0) {
        return -x
    }
    return x
}

# Retorna o menor entre a e b
func min(i64 a, i64 b) i64 {
    if (a < b) {
        return a
    }
    return b
}

# Retorna o maior entre a e b
func max(i64 a, i64 b) i64 {
    if (a > b) {
        return a
    }
    return b
}

# Limita o valor ao intervalo [low, high]
func clamp(i64 x, i64 low, i64 high) i64 {
    if (x < low) {
        return low
    }
    if (x > high) {
        return high
    }
    return x
}

# Retorna o sinal do numero (-1, 0 ou 1)
func sign(i64 x) i32 {
    if (x > 0) {
        return 1
    }
    if (x < 0) {
        return -1
    }
    return 0
}

# Exponenciacao inteira rapida por quadrado
func pow(i64 base, u32 exp) i64 {
    local res : i64 = 1
    local b : i64 = base
    local e : u32 = exp

    while (e > 0) {
        if ((e % 2) != 0) {
            res = res * b
        }
        b = b * b
        e = e / 2
    }

    return res
}

# Raiz quadrada inteira (metodo babilonico / Newton)
func sqrt(u64 n) u64 {
    if (n == 0) {
        return 0
    }
    if (n <= 3) {
        return 1
    }

    local x0 : u64 = n / 2
    local x1 : u64 = (x0 + (n / x0)) / 2

    while (x1 < x0) {
        x0 = x1
        x1 = (x0 + (n / x0)) / 2
    }

    return x0
}

# Maximo divisor comum (Algoritmo de Euclides)
func gcd(i64 a, i64 b) i64 {
    local x : i64 = abs(a)
    local y : i64 = abs(b)

    while (y != 0) {
        local temp : i64 = y
        y = x % y
        x = temp
    }

    return x
}

# Minimo multiplo comum
func lcm(i64 a, i64 b) i64 {
    if (a == 0 || b == 0) {
        return 0
    }
    return abs(a * (b / gcd(a, b)))
}

# Rotacao de bits para a esquerda (64-bit)
func rol64(u64 val, u32 shift) u64 {
    local s : u32 = shift % 64
    if (s == 0) {
        return val
    }
    return (val << s) | (val >> (64 - s))
}

# Rotacao de bits para a direita (64-bit)
func ror64(u64 val, u32 shift) u64 {
    local s : u32 = shift % 64
    if (s == 0) {
        return val
    }
    return (val >> s) | (val << (64 - s))
}

@endif

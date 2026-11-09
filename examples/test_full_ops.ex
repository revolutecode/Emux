[Main]

func Somar(i32 x, i32 y) i32 {
    return x + y
}

func Start() i32 {
    local a : i32 = 100
    local b : i32 = 20
    local c : i32 = (a / b) + (a % b) # (100 / 20 = 5) + (100 % 20 = 0) => 5

    # Testando chamada de funcao
    local resultado : i32 = Somar(c, 37) # 5 + 37 => 42

    return resultado
}
